/*
The MIT License (MIT)

Copyright (c) 2026 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <cstdint>
#include <array>
#include <cmath>

#include "finalizing.hpp"

#include "d3d_service.hpp"
using D3D = d3d_service::D3D;

#include "method_bin_smooth.hpp"
using bin_sm = Border_S::method::bin_smooth;
#include "methods_common.hpp"
using common = Border_S::method::common;

#define ANON_NS_B namespace {
#define ANON_NS_E }


ANON_NS_B;
////////////////////////////////
// shaders.
////////////////////////////////
constexpr char cs_src_pass_2[] = R"(
RWTexture2D<float> dst : register(u0);
StructuredBuffer<uint2> mid : register(t0);
Texture2D<float> disk : register(t1);
cbuffer constant0 : register(b0) {
	int2 size_mid, size_dst;
	int2 range_src_y, range_mid_x;
	int2 delta;
	uint2 size_arc;
	uint stride_mid;
	float thresh, alpha_base;
};
static const uint2 L = size_arc >> 1;
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= uint2(size_dst))) return;

	const uint
		mid_idx_base = stride_mid * id.y - uint(range_mid_x[0]),
		outer_l = alpha_base > 0 ? 0 : ~0u;
	float max_alpha = 0;
	for (int dx = -int(L.x); dx <= int(L.x); dx++) {
		const int x = int(id.x) + dx;
		const uint2 l = (range_mid_x[0] <= x && x < range_mid_x[1]) ?
			mid[uint(x) + mid_idx_base] : outer_l;
		const float2 alpha = l <= L.y ? float2(
			disk[L - uint2(dx, -int(l.x))],
			disk[L - uint2(dx, l.y)]) : 0;
		max_alpha = max(max_alpha, max(alpha.x, alpha.y));
	}
	dst[id] = abs(max_alpha - alpha_base);
}
)";

constexpr char cs_src_arc_L2[] = R"(
RWTexture2D<float> disk : register(u0);
cbuffer constant0 : register(b0) {
	float2 size, delta;
	uint2 size_disk;
	float sup_ell_exp;
};

[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_disk)) return;

	const float2 p = int2(id - (size_disk >> 1)) - delta;
	const float2
		u = size > 0 ? p / size : 0,
		v = size > 0 ? 2 * u / size : 0;
	const float d = 1 - dot(u, u);
	const float D = length(v);
	disk[id] = saturate(D > 0 ? d / D : 1);
}
)"; // usual ellipse. (distance by positive definite quadratic form.)
constexpr char cs_src_arc_L1[] = R"(
RWTexture2D<float> disk : register(u0);
cbuffer constant0 : register(b0) {
	float2 size, delta;
	uint2 size_disk;
	float sup_ell_exp;
};

[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_disk)) return;

	const float2 p = abs(int2(id - (size_disk >> 1)) - delta);
	const float2
		u = size > 0 ? p / size : 0,
		v = size > 0 ? 1 / size : 0;
	const float d = 1 - dot(u, 1);
	const float D = length(v);
	disk[id] = saturate(D > 0 ? d / D : 1);
}
)"; // rhombus (L^1 distance). exp == 1 or rx + ry - 2 * rx * ry >= 0.
constexpr char cs_src_arc_box[] = R"(
RWTexture2D<float> disk : register(u0);
cbuffer constant0 : register(b0) {
	float2 size, delta;
	uint2 size_disk;
	float sup_ell_exp;
};

[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_disk)) return;

	const float2 p = abs(int2(id - (size_disk >> 1)) - delta);
	const float2 d = size > 0 ? size - p : 1;
	disk[id] = saturate(min(d.x, d.y));
}
)"; // L^infty distance (maximum among coordinates).
constexpr char cs_src_arc_cross[] = R"(
RWTexture2D<float> disk : register(u0);
cbuffer constant0 : register(b0) {
	float2 size, delta;
	uint2 size_disk;
	float sup_ell_exp;
};

[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_disk)) return;

	const float2 p = abs(int2(id - (size_disk >> 1)) - delta);
	const float2 d = size > 0 ? size - p : 1;
	disk[id] = saturate(any(p == 0) ? min(d.x, d.y) : 0);
}
)"; // cross shape (only points on the x/y axis are valid).
constexpr char cs_src_arc_Lp[] = R"(
RWTexture2D<float> disk : register(u0);
cbuffer constant0 : register(b0) {
	float2 size, delta;
	uint2 size_disk;
	float sup_ell_exp;
};

[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_disk)) return;

	const float2 p = abs(int2(id - (size_disk >> 1)) - delta);
	const float2
		u = size > 0 ? p / size : 0,
		v = size > 0 ? sup_ell_exp * pow(abs(u), sup_ell_exp - 1) / size : 0;
	const float d = 1 - dot(pow(abs(u), sup_ell_exp), 1);
	const float D = length(v);
	disk[id] = saturate(D > 0 ? d / D : 1);
}
)"; // L^p distance with p > 1.
constexpr char cs_src_arc_cusp[] = R"(
RWTexture2D<float> disk : register(u0);
cbuffer constant0 : register(b0) {
	float2 size, delta;
	uint2 size_disk;
	float sup_ell_exp;
};
static const float2 size2 = size - 0.5 * (1 + size / size.yx);

[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_disk)) return;

	const float2 p = abs(int2(id - (size_disk >> 1)) - delta);
	float l;
	if (any(p == 0)) {
		const float2 d = size - p;
		l = min(d.x, d.y);
	}
	else if (any(p - 0.5 >= size2)) l = 0;
	else {
		const float2 u = p - 0.5;
		float2
			p2 = float2(u.x, size2.y * pow(abs(1 - pow(abs(u.x / size2.x), sup_ell_exp)), 1 / sup_ell_exp)),
			p3 = float2(size2.x * pow(abs(1 - pow(abs(u.y / size2.y), sup_ell_exp)), 1 / sup_ell_exp), u.y);
		p3 -= p2;
		const float L = dot(p3, p3);
		p3 = (L > 0 ? dot(u - p2, p3) / L : 0) * p3 + p2;
		const float2
			q = pow(abs(dot(pow(abs(p3 / size2), sup_ell_exp), 1)), -1 / sup_ell_exp) * p3,
			v = sup_ell_exp * pow(abs(q / size2), sup_ell_exp - 1) / size2;
		const float D = length(v);
		l = dot(q - u, v) / D;
	}
	disk[id] = saturate(l);
}
)"; // superellipse with exp < 1 and rx + ry - 2 * rx * ry < 0.
struct cs_cbuff_arc {
	float size_x, size_y, delta_x, delta_y;
	uint32_t size_disk_x, size_disk_y;
	float sup_ell_exp;

	[[maybe_unused]] uint8_t _pad[4];
};
static_assert(sizeof(cs_cbuff_arc) % 16 == 0);


////////////////////////////////
// Resource managements.
////////////////////////////////
constinit AviUtl2::finalizing::helpers::init_state init_state{};
D3D::ComPtr<::ID3D11ComputeShader> cs_pass_2,
	cs_arc_L2, cs_arc_L1, cs_arc_box, cs_arc_cross, cs_arc_Lp, cs_arc_cusp;
void quit()
{
	cs_pass_2.Reset();
	cs_arc_L2.Reset();
	cs_arc_L1.Reset();
	cs_arc_box.Reset();
	cs_arc_cross.Reset();
	cs_arc_Lp.Reset();
	cs_arc_cusp.Reset();

	init_state.clear();
}
bool init()
{
	// assumes D3D is already initialized.
	init_state.init(&quit, [] {
		return

		#define cs_src(name)	cs_src_##name, "bin_smooth::cs_" #name
			(cs_pass_2 = D3D::create_compute_shader(cs_src(pass_2))) != nullptr &&
			(cs_arc_L2 = D3D::create_compute_shader(cs_src(arc_L2))) != nullptr &&
			(cs_arc_L1 = D3D::create_compute_shader(cs_src(arc_L1))) != nullptr &&
			(cs_arc_box = D3D::create_compute_shader(cs_src(arc_box))) != nullptr &&
			(cs_arc_cross = D3D::create_compute_shader(cs_src(arc_cross))) != nullptr &&
			(cs_arc_Lp = D3D::create_compute_shader(cs_src(arc_Lp))) != nullptr &&
			(cs_arc_cusp = D3D::create_compute_shader(cs_src(arc_cusp))) != nullptr &&
		#undef cs_src

			true;
	});
	return init_state;
}


////////////////////////////////
// Implementations for bin method.
////////////////////////////////
void prepare_disk(double radius_x, double radius_y, double superellipse_exp,
	double delta_x, double delta_y, uint32_t width, uint32_t height,
	::ID3D11UnorderedAccessView* uav_disk)
{
	// choose the shader.
	::ID3D11ComputeShader* cs_arc;
	if (superellipse_exp == 2) [[likely]] cs_arc = cs_arc_L2.Get();
	else if (std::isinf(superellipse_exp)) cs_arc = cs_arc_box.Get();
	else if (superellipse_exp <= 0) cs_arc = cs_arc_cross.Get();
	else if (superellipse_exp > 1) cs_arc = cs_arc_Lp.Get();
	else if (superellipse_exp == 1 || radius_x + radius_y - 2 * radius_x * radius_y >= 0)
		cs_arc = cs_arc_L1.Get();
	else  cs_arc = cs_arc_cusp.Get();

	// prepare cbuffer.
	auto cbuff = D3D::create_const_buffer(cs_cbuff_arc{
		.size_x = static_cast<float>(radius_x), .size_y = static_cast<float>(radius_y),
		.delta_x = static_cast<float>(delta_x), .delta_y = static_cast<float>(delta_y),
		.size_disk_x = width, .size_disk_y = height,
		.sup_ell_exp = static_cast<float>(superellipse_exp),
	});

	// execute shader.
	D3D::cxt->CSSetShader(cs_arc, nullptr, 0);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_disk, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff.GetAddressOf());
	D3D::cxt->Dispatch((width + 1 + ((1 << 3) - 1)) >> 3, (height + ((1 << 3) - 1)) >> 3, 1);

	// cleanup.
	D3D::cxt->ClearState();
}
ANON_NS_E;


////////////////////////////////
// Exported functions.
////////////////////////////////
bool bin_sm::inflate(
	bool deflation,
	int width_src, int height_src,
	int width_dst, int height_dst,
	int offset_x, int offset_y, double delta_x, double delta_y,
	::ID3D11ShaderResourceView* srv_src, ::ID3D11UnorderedAccessView* uav_shape,
	double threshold, double radius_x, double radius_y, double superellipse_exp,
	D3D::cs_views const& disk, D3D::cs_views const& mid)
{
	if (!init() || !common::init()) return false;

	uint32_t width_disk, height_disk;
	bin_sm::buff_spec::get_size_disk(radius_x, radius_y, width_disk, height_disk);

	// prepare the arc buffer.
	prepare_disk(radius_x, radius_y, superellipse_exp, delta_x, delta_y,
		width_disk, height_disk, disk.uav);

	// create constant buffers.
	auto cbuff = D3D::create_const_buffer(common::cs_cbuff_bin_inf_def{
		.size_mid_x = width_src, .size_mid_y = height_dst,
		.size_dst_x = width_dst, .size_dst_y = height_dst,
		.range_src_t = offset_y, .range_src_b = offset_y + height_src,
		.range_mid_l = offset_x, .range_mid_r = offset_x + width_src,
		.size_arc_x = width_disk, .size_arc_y = height_disk,
		.stride_mid = common::buff_spec::stride_mid(width_src, height_src, width_dst, height_dst),
		.thresh = static_cast<float>(threshold),
		.alpha_base = deflation ? 1.0f : 0.0f,
	});
	if (cbuff == nullptr) return false;

	// execute pass_1.
	D3D::cxt->CSSetShader(common::cs_bin_pass_1.Get(), nullptr, 0);
	D3D::cxt->CSSetShaderResources(0, 1, &srv_src);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &mid.uav, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff.GetAddressOf());
	D3D::cxt->Dispatch((width_src + ((1 << 6) - 1)) >> 6, 2, 1);

	// execute pass_2.
	D3D::cxt->CSSetShader(cs_pass_2.Get(), nullptr, 0);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_shape, nullptr); // this unbinds `mid.uav` ...
	::ID3D11ShaderResourceView* const srv_pass_2[] = { mid.srv, disk.srv };
	D3D::cxt->CSSetShaderResources(0, std::size(srv_pass_2), srv_pass_2); // ... so `mid.srv` can be bound here.
	D3D::cxt->Dispatch((width_dst + ((1 << 3) - 1)) >> 3, (height_dst + ((1 << 3) - 1)) >> 3, 1);

	// cleanup.
	D3D::cxt->ClearState();

	return true;
}

void bin_sm::buff_spec::get_size_disk(double radius_x, double radius_y, uint32_t& width, uint32_t& height)
{
	width = 1 + 2 * static_cast<int>(std::ceil(radius_x));
	height = 1 + 2 * static_cast<int>(std::ceil(radius_y));
}

void bin_sm::buff_spec::get_size_mid(int width_src, int height_src, int width_dst, int height_dst, uint32_t& length)
{
	common::buff_spec::get_size_mid(width_src, height_src, width_dst, height_dst, length);

	static_assert(elem_size_mid == common::buff_spec::elem_size_mid);
}
