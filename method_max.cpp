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

#include "method_max.hpp"
using max = Border_S::method::max;
#include "methods_common.hpp"
using common = Border_S::method::common;

#define ANON_NS_B namespace {
#define ANON_NS_E }


ANON_NS_B;
////////////////////////////////
// shaders.
////////////////////////////////
constexpr char cs_src_pass_1[] = R"(
RWTexture2D<float2> mid : register(u0);
Texture2D<float> src : register(t0);
StructuredBuffer<int2> arc : register(t1);
cbuffer constant0 : register(b0) {
	uint2 size_src, size_mid, size_dst;
	int2 offset_dst;
	uint2 size_arc, rod_length;
	float alpha_base;
};
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_mid)) return;

	float2 max_alpha = 0;
	for (int dx = int(rod_length.x); --dx >= 0; ) {
		const uint2 pos_src = id - uint2(dx, 0);
		float alpha = 0;
		if (all(pos_src < size_src))
			alpha = src[pos_src];
		alpha = abs(alpha - alpha_base);
		max_alpha.x = max(max_alpha.x, alpha);
	}
	for (int dy = int(rod_length.y); --dy >= 0; ) {
		const uint2 pos_src = id - uint2(0, dy);
		float alpha = 0;
		if (all(pos_src < size_src))
			alpha = src[pos_src];
		alpha = abs(alpha - alpha_base);
		max_alpha.y = max(max_alpha.y, alpha);
	}
	mid[id] = max_alpha;
}
)";
constexpr char cs_src_pass_2[] = R"(
RWTexture2D<float> dst : register(u0);
Texture2D<float2> mid : register(t0);
StructuredBuffer<int2> arc : register(t1);
cbuffer constant0 : register(b0) {
	uint2 size_src, size_mid, size_dst;
	int2 offset_dst;
	uint2 size_arc, rod_length;
	float alpha_base;
};
static const uint2 L = size_arc >> 1;
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_dst)) return;

	const uint2 pos_mid_0 = id - uint2(offset_dst);
	float max_alpha = 0;
	for (int dy = -int(L.y); dy <= int(L.y); dy++) {
		const int2 l = -arc[L.y + uint(dy)].yx;
		if (l.y - l.x + 1 >= int(rod_length.x)) {
			int dx = l.x - 1;
			do {
				dx = min(l.y, dx + int(rod_length.x));
				const uint2 pos_mid = pos_mid_0 + uint2(dx, dy);
				float alpha = alpha_base;
				if (all(pos_mid < size_mid))
					alpha = mid[pos_mid].x;
				max_alpha = max(max_alpha, alpha);
			} while (dx < l.y);
		}
	}
	for (int dx = -int(L.x); dx <= int(L.x); dx++) {
		const int2 l = -arc[size_arc.y + L.x + uint(dx)].yx;
		if (l.y - l.x + 1 >= int(rod_length.y)) {
			dy = l.x - 1;
			do {
				dy = min(l.y, dy + int(rod_length.y));
				const uint2 pos_mid = pos_mid_0 + uint2(dx, dy);
				float alpha = alpha_base;
				if (all(pos_mid < size_mid))
					alpha = mid[pos_mid].y;
				max_alpha = max(max_alpha, alpha);
			} while (dy < l.y);
		}
	}
	dst[id] = abs(max_alpha - alpha_base);
}
)";
struct cs_cbuff_inf_def {
	uint32_t size_src_x, size_src_y, size_mid_x, size_mid_y, size_dst_x, size_dst_y;
	int32_t offset_dst_x, offset_dst_y;
	uint32_t size_arc_x, size_arc_y, rod_length_x, rod_length_y;
	float alpha_base;

	[[maybe_unused]] uint8_t _pad[12];
};
static_assert(sizeof(cs_cbuff_inf_def) % 16 == 0);


////////////////////////////////
// Resource managements.
////////////////////////////////
constinit AviUtl2::finalizing::helpers::init_state init_state{};
D3D::ComPtr<::ID3D11ComputeShader> cs_pass_1, cs_pass_2;
void quit()
{
	cs_pass_1.Reset();
	cs_pass_2.Reset();

	init_state.clear();
}
bool init()
{
	// assumes D3D is already initialized.
	init_state.init(&quit, [] {
		return

		#define cs_src(name)	cs_src_##name, "sum::cs_" #name
			(cs_pass_1 = D3D::create_compute_shader(cs_src(pass_1))) != nullptr &&
			(cs_pass_2 = D3D::create_compute_shader(cs_src(pass_2))) != nullptr &&
		#undef cs_src

			true;
	});
	return init_state;
}


////////////////////////////////
// Implementations for max method.
////////////////////////////////
void rod_lengths(double radius_x, double radius_y, double superellipse_exp,
	double delta_x, double delta_y, uint32_t& rod_len_x, uint32_t& rod_len_y)
{
	int l, t, r, b;
	if (std::isinf(superellipse_exp) || superellipse_exp <= 0) {
		l = static_cast<int>(std::floor(radius_x - delta_x));
		t = static_cast<int>(std::floor(radius_y - delta_y));
		r = static_cast<int>(std::floor(radius_x + delta_x));
		b = static_cast<int>(std::floor(radius_y + delta_y));
	}
	else if (superellipse_exp >= 1 || radius_x + radius_y - 2 * radius_x * radius_y >= 0) {
		auto const h = std::exp2(-1 / std::max(superellipse_exp, 1.0));
		l = static_cast<int>(std::floor(radius_x * h - delta_x));
		t = static_cast<int>(std::floor(radius_y * h - delta_y));
		r = static_cast<int>(std::floor(radius_x * h + delta_x));
		b = static_cast<int>(std::floor(radius_y * h + delta_y));
	}
	else {
		auto const h = std::exp2(-1 / superellipse_exp);
		double const
			rx2 = radius_x - 0.5 * (1 + radius_x / radius_y),
			ry2 = radius_y - 0.5 * (1 + radius_y / radius_x);
		l = static_cast<int>(std::floor(0.5 + rx2 * h - delta_x));
		t = static_cast<int>(std::floor(0.5 + ry2 * h - delta_y));
		r = static_cast<int>(std::floor(0.5 + rx2 * h + delta_x));
		b = static_cast<int>(std::floor(0.5 + ry2 * h + delta_y));
	}
	rod_len_x = static_cast<uint32_t>(1 + std::max(l + r, 0));
	rod_len_y = static_cast<uint32_t>(1 + std::max(t + b, 0));
}
ANON_NS_E;


////////////////////////////////
// Exported functions.
////////////////////////////////
bool max::inflate(
	bool deflation,
	int width_src, int height_src,
	int width_dst, int height_dst,
	int offset_x, int offset_y, double delta_x, double delta_y,
	::ID3D11ShaderResourceView* srv_src, ::ID3D11UnorderedAccessView* uav_shape,
	double radius_x, double radius_y, double superellipse_exp,
	D3D::cs_views const& arc, D3D::cs_views const& mid)
{
	if (!init() || !common::init()) return false;

	// calculate coordinates.
	uint32_t const
		arc_width = 1 + 2 * static_cast<uint32_t>(radius_x),
		arc_height = 1 + 2 * static_cast<uint32_t>(radius_y);
	uint32_t rod_len_x, rod_len_y;
	rod_lengths(radius_x, radius_y, superellipse_exp, delta_x, delta_y, rod_len_x, rod_len_y);

	// prepare the arc buffers.
	common::buff_spec::prepare_arc(radius_x, radius_y, superellipse_exp,
		delta_x, delta_y, arc_width, arc_height, arc.uav);

	// create constant buffer.
	auto cbuff = D3D::create_const_buffer(cs_cbuff_inf_def{
		.size_src_x = static_cast<uint32_t>(width_src), .size_src_y = static_cast<uint32_t>(height_src),
		.size_mid_x = static_cast<uint32_t>(width_src) + rod_len_x,
		.size_mid_y = static_cast<uint32_t>(height_src) + rod_len_y,
		.size_dst_x = static_cast<uint32_t>(width_dst), .size_dst_y = static_cast<uint32_t>(height_dst),
		.offset_dst_x = offset_x, .offset_dst_y = offset_y,
		.size_arc_x = arc_width, .size_arc_y = arc_height,
		.rod_length_x = rod_len_x, .rod_length_y = rod_len_y,
		.alpha_base = deflation ? 1.0f : 0.0f,
	});
	if (cbuff == nullptr) return false;

	// execute pass_1.
	D3D::cxt->CSSetShader(cs_pass_1.Get(), nullptr, 0);
	::ID3D11ShaderResourceView* const srv_pass_1[] = { srv_src, arc.srv };
	D3D::cxt->CSSetShaderResources(0, std::size(srv_pass_1), srv_pass_1);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &mid.uav, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff.GetAddressOf());
	D3D::cxt->Dispatch(
		(width_src + rod_len_x + ((1 << 3) - 1)) >> 3,
		(height_src + rod_len_y + ((1 << 3) - 1)) >> 3, 1);

	// execute pass_2.
	D3D::cxt->CSSetShader(cs_pass_2.Get(), nullptr, 0);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_shape, nullptr); // this unbinds `mid.uav` ...
	::ID3D11ShaderResourceView* const srv_pass_2[] = { mid.srv, arc.srv };
	D3D::cxt->CSSetShaderResources(0, std::size(srv_pass_2), srv_pass_2); // ... so `mid.srv` can be bound here.
	D3D::cxt->Dispatch((width_dst + ((1 << 3) - 1)) >> 3, (height_dst + ((1 << 3) - 1)) >> 3, 1);

	// cleanup.
	D3D::cxt->ClearState();

	return true;
}

void max::buff_spec::get_size_arc(double radius_x, double radius_y, uint32_t& length)
{
	int const
		width = 1 + 2 * static_cast<int>(std::ceil(radius_x)),
		height = 1 + 2 * static_cast<int>(std::ceil(radius_y));
	length = elem_size_arc * (width + height);

	static_assert(elem_size_arc == common::buff_spec::elem_size_arc);
}

void max::buff_spec::get_size_mid(double radius_x, double radius_y, double superellipse_exp,
	int width_src, int height_src, int width_dst, int height_dst,
	uint32_t& width, uint32_t& height)
{
	uint32_t rod_len_x, rod_len_y;
	rod_lengths(radius_x, radius_y, superellipse_exp, 0, 0, rod_len_x, rod_len_y);
	rod_len_x += 2; rod_len_y += 2; // 2 pixels of inflation at most.
	width = static_cast<uint32_t>(width_src) + rod_len_x;
	height = static_cast<uint32_t>(height_src) + rod_len_y;
}
