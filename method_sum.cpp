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
#include <bit>

#include "finalizing.hpp"

#include "d3d_service.hpp"
using D3D = d3d_service::D3D;

#include "method_sum.hpp"
using sum = Border_S::method::sum;

#define ANON_NS_B namespace {
#define ANON_NS_E }


ANON_NS_B;
////////////////////////////////
// shaders.
////////////////////////////////
constexpr char cs_src_pass_1[] = R"(
struct mid_sum {
	int side;
	float outer;
};
RWStructuredBuffer<mid_sum> mid : register(u0);
Texture2D<float> src : register(t0);
StructuredBuffer<uint2> arc : register(t1);
Texture2D<float> disk : register(t2);
cbuffer constant0 : register(b0) {
	int4 range_src;
	uint2 size_mid, size_dst;
	int2 offset_mid, offset_dst;
	int2 half_size_disk;
	uint stride_mid;
	float alpha_rate;
};
int quantize(float v)
{
	return int(round((1 << 16) * v));
}
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_mid)) return;

	const int2 pos_src_0 = int2(id) - offset_mid;
	int sum_side = 0; float sum_outer = 0;
	for (int dy = -half_size_disk.y; dy <= half_size_disk.y; dy++) {
		const int2 range_x = arc[dy + half_size_disk.y];
		int dx = range_x.x - 1;
		int2 pos_src = pos_src_0 + int2(dx, dy);
		sum_side += quantize(dx >= 0 && all(range_src.xy <= pos_src && pos_src < range_src.zw) ?
			src[uint2(pos_src)] : 0);

		pos_src = pos_src_0 + int2(-dx - 1, dy);
		sum_side -= quantize(dx >= 0 && all(range_src.xy <= pos_src && pos_src < range_src.zw) ?
			src[uint2(pos_src)] : 0);

		for (dx = range_x.x; dx < range_x.y; dx++) {
			pos_src = pos_src_0 + int2(dx, dy);
			sum_outer += disk[uint2(half_size_disk + int2(dx, dy))] *
				(all(range_src.xy <= pos_src && pos_src < range_src.zw) ? src[uint2(pos_src)] : 0);
			pos_src = pos_src_0 + int2(-dx, dy);
			sum_outer += disk[uint2(half_size_disk + int2(-dx, dy))] *
				(all(range_src.xy <= pos_src && pos_src < range_src.zw) ? src[uint2(pos_src)] : 0);
		}
	}
	const mid_sum sum = { sum_side, sum_outer };
	mid[id.x + stride_mid * id.y] = sum;
}
)";

constexpr char cs_src_pass_2_inf[] = R"(
struct mid_sum {
	int side;
	float outer;
};
RWTexture2D<float> dst : register(u0);
StructuredBuffer<mid_sum> mid : register(t0);
StructuredBuffer<uint2> arc : register(t1);
Texture2D<float> disk : register(t2);
cbuffer constant0 : register(b0) {
	int4 range_src;
	uint2 size_mid, size_dst;
	int2 offset_mid, offset_dst;
	int2 half_size_disk;
	uint stride_mid;
	float alpha_rate;
};
static const float2 u64_to_float = float2(1.0 / (1 << 16), 1.0 * (1u << (32 - 16)));
[numthreads(1, 64, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if ((id >= size_dst).y) return;

	const uint mid_idx_base = stride_mid * id.y;
	uint2 sum_side = 0;
	for (int x = min(offset_dst.x, 0); x < int(size_dst.x); x++) {
		const uint pos_mid_x = uint(x - offset_dst.x);
		int side = 0; float outer = 0;
		if (pos_mid_x < size_mid.x) {
			const mid_sum m = mid[pos_mid_x + mid_idx_base];
			side = m.side; outer = m.outer;
		}

		sum_side.x += uint(side); sum_side.y += uint(side >> 31) + (sum_side.x < uint(side) ? 1 : 0);
		if (x >= 0) dst[uint2(x, id.y)] =
			saturate(alpha_rate * (dot(float2(sum_side), u64_to_float) + outer));
	}
}
)";
constexpr char cs_src_pass_2_def[] = R"(
struct mid_sum {
	int side;
	float outer;
};
RWTexture2D<float> dst : register(u0);
StructuredBuffer<mid_sum> mid : register(t0);
StructuredBuffer<uint2> arc : register(t1);
Texture2D<float> disk : register(t2);
cbuffer constant0 : register(b0) {
	int4 range_src;
	uint2 size_mid, size_dst;
	int2 offset_mid, offset_dst;
	int2 half_size_disk;
	uint stride_mid;
	float alpha_rate;
};
static const float2 u64_to_float = float2(1.0 / (1 << 16), 1.0 * (1u << (32 - 16)));
[numthreads(1, 64, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if ((id >= size_dst).y) return;

	uint2 sum_side = 0; float sum_outer = 0;
	for (int y = -half_size_disk.y; y <= half_size_disk.y; y++) {
		const int2 range_x = arc[half_size_disk.y + y];
		const uint n = max(2 * range_x.x - 1, 0) << 16;
		sum_side.x += n; sum_side.y += sum_side.x < n ? 1 : 0;

		for (int x = range_x.x; x < range_x.y; x++)
			sum_outer += disk[uint2(half_size_disk + int2(x, y))]
				+ disk[uint2(half_size_disk + int2(-x, y))];
	}

	const uint mid_idx_base = stride_mid * id.y;
	for (int x = min(offset_dst.x, 0); x < int(size_dst.x); x++) {
		const uint pos_mid_x = uint(x - offset_dst.x);
		int side = 0; float outer = 0;
		if (pos_mid_x < size_mid.x) {
			const mid_sum m = mid[pos_mid_x + mid_idx_base];
			side = -m.side; outer = m.outer;
		}

		sum_side.x += uint(side); sum_side.y += uint(side >> 31) + (sum_side.x < uint(side) ? 1 : 0);
		if (x >= 0) dst[uint2(x, id.y)] =
			1 - saturate(alpha_rate * (dot(float2(sum_side), u64_to_float) + (sum_outer - outer)));
	}
}
)";
struct cs_cbuff_inf_def_sum {
	int32_t range_src_l, range_src_t, range_src_r, range_src_b;
	uint32_t size_mid_x, size_mid_y, size_dst_x, size_dst_y;
	int32_t offset_mid_x, offset_mid_y, offset_dst_x, offset_dst_y;
	uint32_t half_size_disk_x, half_size_disk_y;
	uint32_t stride_mid; float alpha_rate;
};
static_assert(sizeof(cs_cbuff_inf_def_sum) % 16 == 0);

constexpr char cs_src_arc_L2[] = R"(
RWTexture2D<float> disk : register(u0);
RWStructuredBuffer<uint2> arc : register(u1);
cbuffer constant0 : register(b0) {
	float2 aspect;
	uint2 size_disk;
	float radius, sup_ell_exp;
};
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_disk + uint2(1, 0))) return;

	[branch] if (id.x < size_disk.x) {
		const float2 p = aspect > 0 ? int2(id - (size_disk >> 1)) / aspect : 0;
		const float r = length(p);
		disk[id] = saturate(2 * (radius - r));
	}
	else {
		const float rel_y = aspect.y > 0 ? abs(int(id.y - (size_disk.y >> 1))) / aspect.y : 0;
		const float
			q0 = radius * radius - rel_y * rel_y,
			q1 = max(radius - 0.5, 0) * max(radius - 0.5, 0) - rel_y * rel_y;
		const float
			x0 = q0 >= 0 ? aspect.x * sqrt(q0) : -1,
			x1 = q1 >= 0 ? aspect.x * sqrt(q1) : -1;
		arc[id.y] = uint2(floor(x1 + 1), floor(x0 + 1));
	}
}
)"; // usual ellipse. (distance by positive definite quadratic form.)
constexpr char cs_src_arc_L1[] = R"(
RWTexture2D<float> disk : register(u0);
RWStructuredBuffer<uint2> arc : register(u1);
cbuffer constant0 : register(b0) {
	float2 aspect;
	uint2 size_disk;
	float radius, sup_ell_exp;
};
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_disk + uint2(1, 0))) return;

	[branch] if (id.x < size_disk.x) {
		const float2 p = aspect > 0 ? int2(id - (size_disk >> 1)) / aspect : 0;
		const float r = dot(abs(p), 1);
		disk[id] = saturate(2 * (radius - r));
	}
	else {
		const float rel_y = aspect.y > 0 ? abs(int(id.y - (size_disk.y >> 1))) / aspect.y : 0;
		const float
			q0 = radius - rel_y,
			q1 = (radius - 0.5) - rel_y;
		const float
			x0 = q0 >= 0 ? aspect.x * q0 : -1,
			x1 = q1 >= 0 ? aspect.x * q1 : -1;
		arc[id.y] = uint2(floor(x1 + 1), floor(x0 + 1));
	}
}
)"; // rhombus (L^1 distance). exp == 1 or rx + ry - 2 * rx * ry >= 0.
constexpr char cs_src_arc_box[] = R"(
RWTexture2D<float> disk : register(u0);
RWStructuredBuffer<uint2> arc : register(u1);
cbuffer constant0 : register(b0) {
	float2 aspect;
	uint2 size_disk;
	float radius, sup_ell_exp;
};
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_disk + uint2(1, 0))) return;

	[branch] if (id.x < size_disk.x) {
		const float2 p = aspect > 0 ? int2(id - (size_disk >> 1)) / aspect : 0;
		const float r = max(abs(p.x), abs(p.y));
		disk[id] = saturate(2 * (radius - r));
	}
	else {
		const float rel_y = aspect.y > 0 ? abs(int(id.y - (size_disk.y >> 1))) / aspect.y : 0;
		const float
			q0 = radius - rel_y,
			q1 = (radius - 0.5) - rel_y;
		const float
			x0 = q0 >= 0 ? aspect.x * radius : -1,
			x1 = q1 >= 0 ? aspect.x * (radius - 0.5) : -1;
		arc[id.y] = uint2(floor(x1 + 1), floor(x0 + 1));
	}
}
)"; // L^infty distance (maximum among coordinates).
constexpr char cs_src_arc_cross[] = R"(
RWTexture2D<float> disk : register(u0);
RWStructuredBuffer<uint2> arc : register(u1);
cbuffer constant0 : register(b0) {
	float2 aspect;
	uint2 size_disk;
	float radius, sup_ell_exp;
};
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_disk + uint2(1, 0))) return;

	[branch] if (id.x < size_disk.x) {
		const float2 p = aspect > 0 ? abs(int2(id - (size_disk >> 1))) / aspect : 0;
		const float r = min(p.x, p.y);
		disk[id] = saturate(2 * (radius - r));
	}
	else {
		const int y = abs(int(id.y - (size_disk.y >> 1)));
		arc[id.y] = y == 0 ? uint2(floor(aspect.x * (radius - 0.5)) + 1, floor(aspect.x * radius) + 1) :
			y <= aspect.y * (radius - 0.5) ? uint2(1, 1) :
			y <= aspect.y * radius ? uint2(0, 1) : uint2(0, 0);
	}
}
)"; // cross shape (only points on the x/y axis are valid).
constexpr char cs_src_arc_Lp[] = R"(
RWTexture2D<float> disk : register(u0);
RWStructuredBuffer<uint2> arc : register(u1);
cbuffer constant0 : register(b0) {
	float2 aspect;
	uint2 size_disk;
	float radius, sup_ell_exp;
};
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_disk + uint2(1, 0))) return;

	[branch] if (id.x < size_disk.x) {
		const float2 p = aspect > 0 ? int2(id - (size_disk >> 1)) / aspect : 0;
		const float r = pow(dot(pow(abs(p), sup_ell_exp), 1), 1 / sup_ell_exp);
		disk[id] = saturate(2 * (radius - r));
	}
	else {
		const float rel_y = aspect.y > 0 ? abs(int(id.y - (size_disk.y >> 1))) / aspect.y : 0;
		const float
			q0 = 1 - pow(abs(rel_y / radius), sup_ell_exp),
			q1 = radius - 0.5 >= rel_y ? 1 - pow(abs(rel_y > 0 ? rel_y / (radius - 0.5) : 0), sup_ell_exp) : -1;
		const float
			x0 = q0 >= 0 ? aspect.x * radius * pow(abs(q0), 1 / sup_ell_exp) : -1,
			x1 = q1 >= 0 ? aspect.x * (radius - 0.5) * pow(abs(q1), 1 / sup_ell_exp) : -1;
		arc[id.y] = uint2(floor(x1 + 1), floor(x0 + 1));
	}
}
)"; // L^p distance with p > 1.
constexpr char cs_src_arc_cusp[] = R"(
RWTexture2D<float> disk : register(u0);
RWStructuredBuffer<uint2> arc : register(u1);
cbuffer constant0 : register(b0) {
	float2 aspect;
	uint2 size_disk;
	float radius, sup_ell_exp;
};
static const float2 radius2_raw = radius * aspect - 0.5 * (1 + aspect / aspect.yx);
static const float radius2 = max(radius2_raw.x, radius2_raw.y);
static const float2 aspect2 = radius2_raw / radius2;

[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_disk + uint2(1, 0))) return;

	[branch] if (id.x < size_disk.x) {
		const int2 p = abs(int2(id - (size_disk >> 1)));
		float r;
		if (any(p == 0)) r = radius - dot(p / aspect, 1);
		else {
			const float2 p2 = (p - 0.5) / aspect2;
			r = radius2 * (1 - pow(dot(pow(abs(p2 / radius2), sup_ell_exp), 1), 1 / sup_ell_exp));
		}
		disk[id] = saturate(2 * r);
	}
	else {
		const float rel_y1 = abs(int(id.y - (size_disk.y >> 1))) / aspect.y;
		float2 x;
		if (rel_y1 == 0) x = aspect.x * float2(radius - 0.5, radius) + 1;
		else if (rel_y1 > radius) x = 0;
		else {
			const float rel_y2 = (abs(int(id.y - (size_disk.y >> 1))) - 0.5) / aspect2.y;
			const float
				q0 = 1 - pow(abs(rel_y2 / radius2), sup_ell_exp),
				q1 = 1 - pow(abs(rel_y2 / (radius2 - 0.5)), sup_ell_exp);
			x = rel_y2 <= float2(radius2 - 0.5, radius2) ?
				aspect2.x * float2(
					(radius2 - 0.5) * pow(abs(q1), 1 / sup_ell_exp),
					radius2 * pow(abs(q0), 1 / sup_ell_exp)) + 1.5 : 0;
		}
		arc[id.y] = uint2(floor(x));
	}
}
)"; // superellipse with exp < 1 and rx + ry - 2 * rx * ry < 0.
struct cs_cbuff_arc {
	float aspect_x, aspect_y;
	uint32_t size_disk_x, size_disk_y;
	float radius, sup_ell_exp;

	[[maybe_unused]] uint8_t _pad[8];
};
static_assert(sizeof(cs_cbuff_arc) % 16 == 0);


////////////////////////////////
// Resource managements.
////////////////////////////////
constinit AviUtl2::finalizing::helpers::init_state init_state{};
D3D::ComPtr<::ID3D11ComputeShader> cs_pass_1, cs_pass_2_inf, cs_pass_2_def,
	cs_arc_L2, cs_arc_L1, cs_arc_box, cs_arc_cross, cs_arc_Lp, cs_arc_cusp;
void quit()
{
	cs_pass_1.Reset();
	cs_pass_2_inf.Reset();
	cs_pass_2_def.Reset();
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

		#define cs_src(name)	cs_src_##name, "sum::cs_" #name
			(cs_pass_1 = D3D::create_compute_shader(cs_src(pass_1))) != nullptr &&
			(cs_pass_2_inf = D3D::create_compute_shader(cs_src(pass_2_inf))) != nullptr &&
			(cs_pass_2_def = D3D::create_compute_shader(cs_src(pass_2_def))) != nullptr &&
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
// Implementations for sum method.
////////////////////////////////
inline double practical_expo(double superellipse_exp)
{
	constexpr double thresh = 232.0;
	if (std::isinf(superellipse_exp) || superellipse_exp <= 0)
		return superellipse_exp;
	return std::min(std::max(superellipse_exp, 1 / thresh), thresh);
}
double find_alpha_rate(double rad_short, double rad_long, double superellipse_exp, double blurness)
{
	auto const est_sum = [e = superellipse_exp](double r, double dr, double asp) -> double {
		if (std::isinf(e)) return asp * r / 2;
		else if (e > 0) {
			auto const q = std::pow(std::pow(r, e) - std::pow(std::max(r - dr, 0.0), e), 1 / e);
			return asp * std::max(q - 0.5, 0.0) + 0.5;
		}
		else return 0.5;
	};
	auto const wt_center = [](double r, double asp) -> double {
		if (asp * r < 1) return std::min(2 * r, 1.0);
		auto n = std::floor(std::max(r - 0.5, 0.0) * asp);
		auto m = std::min(std::max(2 * (r - (n + 1) / asp), 0.0), 1.0);
		return 2 * n + 1 + 2 * m;
	};
	auto const sum_half = (est_sum(rad_long, 1, rad_short / rad_long)
		+ est_sum(rad_long - 0.5, 0.5, rad_short / rad_long)) / 2;
	return 1 / std::min(wt_center(rad_long, rad_short / rad_long), 1 + (2 * sum_half - 1) * blurness);
}

void prepare_arc(double radius_x, double radius_y, double superellipse_exp, uint32_t width, uint32_t height,
	::ID3D11UnorderedAccessView* uav_disk, ::ID3D11UnorderedAccessView* uav_arc)
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
		.aspect_x = radius_x < radius_y ? static_cast<float>(radius_x / radius_y) : 1,
		.aspect_y = radius_x > radius_y ? static_cast<float>(radius_y / radius_x) : 1,
		.size_disk_x = width, .size_disk_y = height,
		.radius = static_cast<float>(std::max(radius_x, radius_y)),
		.sup_ell_exp = static_cast<float>(superellipse_exp),
	});

	// execute shader.
	D3D::cxt->CSSetShader(cs_arc, nullptr, 0);
	::ID3D11UnorderedAccessView* uav_arc_disk[] = { uav_disk, uav_arc };
	D3D::cxt->CSSetUnorderedAccessViews(0, std::size(uav_arc_disk), uav_arc_disk, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff.GetAddressOf());
	D3D::cxt->Dispatch((width + 1 + ((1 << 3) - 1)) >> 3, (height + ((1 << 3) - 1)) >> 3, 1);

	// cleanup.
	D3D::cxt->ClearState();
}

constexpr uint32_t bit_ceil_0(uint32_t x)
{
	return x == 0 ? 0 : std::bit_ceil(x);
}

uint32_t stride_mid(double radius_x, double radius_y,
	int width_dst, int height_dst, int offset_x, int offset_y)
{
	struct one {
		uint32_t side;
		float outer;
	};
	static_assert(sizeof(one) == sum::buff_spec::elem_size_mid);
	static_assert(D3D::buffer_stride_align % sizeof(one) == 0);
	constexpr uint32_t unit_align = D3D::buffer_stride_align / sizeof(one);

	auto const half_size_disk_x = static_cast<int>(std::floor(radius_x));
	uint32_t const width = static_cast<uint32_t>(width_dst + std::min(2 * half_size_disk_x, half_size_disk_x - offset_x));
	return (width & (~unit_align + 1)) + bit_ceil_0(width & (unit_align - 1));
}
ANON_NS_E;


////////////////////////////////
// Exported functions.
////////////////////////////////
bool sum::inflate(
	bool deflation,
	int width_src, int height_src,
	int width_dst, int height_dst,
	int offset_x, int offset_y,
	::ID3D11ShaderResourceView* srv_src, ::ID3D11UnorderedAccessView* uav_shape,
	double blurness, double radius_x, double radius_y, double superellipse_exp,
	D3D::cs_views const& disk, D3D::cs_views const& arc, D3D::cs_views const& mid)
{
	if (!init()) return false;

	uint32_t width_disk, height_disk;
	buff_spec::get_size_disk(radius_x, radius_y, width_disk, height_disk);
	const uint32_t half_size_disk_x = width_disk >> 1, half_size_disk_y = height_disk >> 1;

	// calculate coordinates.
	int const
		// offset of top-left of src relative to mid (x_src = x_mid - offset_mid).
		offset_mid_x = std::min(offset_x + 2 * static_cast<int>(half_size_disk_x), static_cast<int>(half_size_disk_x)), // offset_mid_y = offset_y.
		// offset of top-left of mid relative to dst (x_mid = x_dst - offset_dst).
		offset_dst_x = offset_x - offset_mid_x, // offset_dst_y = offset_y - offset_mid_y = 0.
		size_mid_x = width_dst - offset_dst_x; // size_mid_y = height_dst.

	// prepare the arc buffers.
	prepare_arc(radius_x, radius_y, superellipse_exp,
		1 + 2 * half_size_disk_x, 1 + 2 * half_size_disk_y, disk.uav, arc.uav);

	// create constant buffer.
	auto cbuff = D3D::create_const_buffer(cs_cbuff_inf_def_sum{
		.range_src_l = static_cast<int>(half_size_disk_x) - offset_mid_x,
		.range_src_t = std::max(-offset_y - static_cast<int>(half_size_disk_y), 0),
		.range_src_r = std::min(width_src, width_dst - offset_x + static_cast<int>(half_size_disk_x)),
		.range_src_b = std::min(height_src, height_dst - offset_y + static_cast<int>(half_size_disk_y)),

		.size_mid_x = static_cast<uint32_t>(size_mid_x), .size_mid_y = static_cast<uint32_t>(height_dst),
		.size_dst_x = static_cast<uint32_t>(width_dst), .size_dst_y = static_cast<uint32_t>(height_dst),

		.offset_mid_x = offset_mid_x, .offset_mid_y = offset_y,
		.offset_dst_x = offset_dst_x, .offset_dst_y = 0,

		.half_size_disk_x = half_size_disk_x, .half_size_disk_y = half_size_disk_y,
		.stride_mid = stride_mid(radius_x, radius_y, width_dst, height_dst, offset_x, offset_y),
		.alpha_rate = static_cast<float>(find_alpha_rate(
			std::min(radius_x, radius_y), std::max(radius_x, radius_y), superellipse_exp, blurness)),
	});
	if (cbuff == nullptr) return false;

	// execute pass_1.
	D3D::cxt->CSSetShader(cs_pass_1.Get(), nullptr, 0);
	::ID3D11ShaderResourceView* const srv_pass_1[] = { srv_src, arc.srv, disk.srv };
	D3D::cxt->CSSetShaderResources(0, std::size(srv_pass_1), srv_pass_1);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &mid.uav, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff.GetAddressOf());
	D3D::cxt->Dispatch((size_mid_x + ((1 << 3) - 1)) >> 3, (height_dst + ((1 << 3) - 1)) >> 3, 1);

	// execute pass_2.
	D3D::cxt->CSSetShader(deflation ? cs_pass_2_def.Get() : cs_pass_2_inf.Get(), nullptr, 0);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_shape, nullptr); // this unbinds `mid.uav` ...
	::ID3D11ShaderResourceView* const srv_pass_2[] = { mid.srv, arc.srv, disk.srv };
	D3D::cxt->CSSetShaderResources(0, std::size(srv_pass_2), srv_pass_2); // ... so `mid.srv` can be bound here.
	D3D::cxt->Dispatch(1, (height_dst + ((1 << 6) - 1)) >> 6, 1);

	// cleanup.
	D3D::cxt->ClearState();

	return true;
}

void sum::buff_spec::get_size_disk(double radius_x, double radius_y, uint32_t& width, uint32_t& height)
{
	width = 1 + 2 * static_cast<uint32_t>(std::floor(radius_x));
	height = 1 + 2 * static_cast<uint32_t>(std::floor(radius_y));
}
void sum::buff_spec::get_size_arc(double radius_x, double radius_y, uint32_t& length)
{
	auto const count = 1 + 2 * static_cast<uint32_t>(std::floor(radius_y));
	length = elem_size_arc * count;
}
void sum::buff_spec::get_size_mid(double radius_x, double radius_y,
	int width_dst, int height_dst, int offset_x, int offset_y, uint32_t& length)
{
	uint32_t const
		stride = stride_mid(radius_x, radius_y, width_dst, height_dst, offset_x, offset_y),
		height = static_cast<uint32_t>(height_dst);
	length = elem_size_mid * stride * height;
}
