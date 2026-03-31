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

#include "methods_common.hpp"
using common = Border_S::method::common;

#define ANON_NS_B namespace {
#define ANON_NS_E }


ANON_NS_B
////////////////////////////////
// shaders.
////////////////////////////////
constexpr char cs_src_pass_1[] = R"(
RWStructuredBuffer<uint2> mid : register(u0);
Texture2D<float> src : register(t0);
cbuffer constant0 : register(b0) {
	int2 size_mid, size_dst;
	int2 range_src_y, range_mid_x;
	int2 delta;
	uint2 size_arc;
	uint stride_mid;
	float thresh, alpha_base;
};
[numthreads(64, 1, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if ((id >= uint2(size_mid)).x) return;

	uint dist = alpha_base > 0 ? 0 : 1 << 29;
	[branch] if (id.y == 0) {
		for (int y = min(0, range_src_y[0]); y < size_mid.y; y++, dist++) {
			const float alpha = (range_src_y[0] <= y && y < range_src_y[1]) ?
				src[uint2(id.x, y - range_src_y[0])] : 0;
			if (abs(alpha - alpha_base) > thresh)
				dist = 0;
			if (y >= 0) mid[id.x + stride_mid * uint(y)].x = dist;
		}
	}
	else {
		for (int y = max(size_mid.y, range_src_y[1]); --y >= 0; dist++) {
			const float alpha = (range_src_y[0] <= y && y < range_src_y[1]) ?
				src[uint2(id.x, y - range_src_y[0])] : 0;
			if (abs(alpha - alpha_base) > thresh)
				dist = 0;
			if (y < size_mid.y) mid[id.x + stride_mid * uint(y)].y = dist;
		}
	}
}
)";

constexpr char cs_src_arc_L2[] = R"(
RWStructuredBuffer<int2> arc : register(u0);
cbuffer constant0 : register(b0) {
	float2 size, delta;
	uint2 size_arc;
	float sup_ell_exp;
};
[numthreads(64, 1, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (id.x >= size_arc.x + size_arc.y) return;

	int V; float2 sz, d;
	if (id.x < size_arc.y) {
		V = 2 * int(id.x) - int(size_arc.y) + 1;
		sz = size; d = delta;
	}
	else {
		V = 2 * int(id.x - size_arc.y) - int(size_arc.x) + 1;
		sz = size.yx; d = delta.yx;
	}
	const float v = (abs(V) >> 1) + (V < -1 ? +d.y : V > +1 ? -d.y : abs(d.y));

	const float rel_v = sz.y > 0 ? v / sz.y : 0;
	const float q = 1 - rel_v * rel_v;
	const float u = q >= 0 ? sz.x * sqrt(q) : -1;
	arc[id.x] = u >= 0 ? int2(ceil(d.x - u), floor(d.x + u)) : int2(1, -1);
}
)"; // usual ellipse. (distance by positive definite quadratic form.)
constexpr char cs_src_arc_L1[] = R"(
RWStructuredBuffer<int2> arc : register(u0);
cbuffer constant0 : register(b0) {
	float2 size, delta;
	uint2 size_arc;
	float sup_ell_exp;
};
[numthreads(64, 1, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (id.x >= size_arc.x + size_arc.y) return;

	int V; float2 sz, d;
	if (id.x < size_arc.y) {
		V = 2 * int(id.x) - int(size_arc.y) + 1;
		sz = size; d = delta;
	}
	else {
		V = 2 * int(id.x - size_arc.y) - int(size_arc.x) + 1;
		sz = size.yx; d = delta.yx;
	}
	const float v = (abs(V) >> 1) + (V < -1 ? +d.y : V > +1 ? -d.y : abs(d.y));

	const float u = sz.y > 0 ? (sz.x * sz.y - v * sz.x) / sz.y : sz.x;
	arc[id.x] = u >= 0 ? int2(ceil(d.x - u), floor(d.x + u)) : int2(1, -1);
}
)"; // rhombus (L^1 distance). exp == 1 or rx + ry - 2 * rx * ry >= 0.
constexpr char cs_src_arc_box[] = R"(
RWStructuredBuffer<int2> arc : register(u0);
cbuffer constant0 : register(b0) {
	float2 size, delta;
	uint2 size_arc;
	float sup_ell_exp;
};
[numthreads(64, 1, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (id.x >= size_arc.x + size_arc.y) return;

	int V; float2 sz, d;
	if (id.x < size_arc.y) {
		V = 2 * int(id.x) - int(size_arc.y) + 1;
		sz = size; d = delta;
	}
	else {
		V = 2 * int(id.x - size_arc.y) - int(size_arc.x) + 1;
		sz = size.yx; d = delta.yx;
	}
	const float v = (abs(V) >> 1) + (V < -1 ? +d.y : V > +1 ? -d.y : abs(d.y));

	const float u = v <= sz.y ? sz.x : -1;
	arc[id.x] = u >= 0 ? int2(ceil(d.x - u), floor(d.x + u)) : int2(1, -1);
}
)"; // L^infty distance (maximum among coordinates).
constexpr char cs_src_arc_cross[] = R"(
RWStructuredBuffer<int2> arc : register(u0);
cbuffer constant0 : register(b0) {
	float2 size, delta;
	uint2 size_arc;
	float sup_ell_exp;
};
[numthreads(64, 1, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (id.x >= size_arc.x + size_arc.y) return;

	int V; float2 sz, d;
	if (id.x < size_arc.y) {
		V = 2 * int(id.x) - int(size_arc.y) + 1;
		sz = size; d = delta;
	}
	else {
		V = 2 * int(id.x - size_arc.y) - int(size_arc.x) + 1;
		sz = size.yx; d = delta.yx;
	}
	const float v = (abs(V) >> 1) + (V < -1 ? +d.y : V > +1 ? -d.y : abs(d.y));

	const float u = v > 0.5 ? v > sz.y ? -1 : 0 : sz.x;
	arc[id.x] = u >= 0 ? int2(ceil(d.x - u), floor(d.x + u)) : int2(1, -1);
}
)"; // cross shape (only points on the x/y axis are valid).
constexpr char cs_src_arc_Lp[] = R"(
RWStructuredBuffer<int2> arc : register(u0);
cbuffer constant0 : register(b0) {
	float2 size, delta;
	uint2 size_arc;
	float sup_ell_exp;
};
[numthreads(64, 1, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (id.x >= size_arc.x + size_arc.y) return;

	int V; float2 sz, d;
	if (id.x < size_arc.y) {
		V = 2 * int(id.x) - int(size_arc.y) + 1;
		sz = size; d = delta;
	}
	else {
		V = 2 * int(id.x - size_arc.y) - int(size_arc.x) + 1;
		sz = size.yx; d = delta.yx;
	}
	const float v = (abs(V) >> 1) + (V < -1 ? +d.y : V > +1 ? -d.y : abs(d.y));

	const float rel_v = sz.y > 0 ? v / sz.y : 0;
	const float q = 1 - pow(abs(rel_v), sup_ell_exp);
	const float u = q >= 0 ? sz.x * pow(abs(q), 1 / sup_ell_exp) : -1;
	arc[id.x] = u >= 0 ? int2(ceil(d.x - u), floor(d.x + u)) : int2(1, -1);
}
)"; // L^p distance with p > 1.
constexpr char cs_src_arc_cusp[] = R"(
RWStructuredBuffer<int2> arc : register(u0);
cbuffer constant0 : register(b0) {
	float2 size, delta;
	uint2 size_arc;
	float sup_ell_exp;
};
static const float2 size2 = size - 0.5 * (1 + size / size.yx);

[numthreads(64, 1, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (id.x >= size_arc.x + size_arc.y) return;

	int V; float2 sz, sz2, d;
	if (id.x < size_arc.y) {
		V = 2 * int(id.x) - int(size_arc.y) + 1;
		sz = size; sz2 = size2; d = delta;
	}
	else {
		V = 2 * int(id.x - size_arc.y) - int(size_arc.x) + 1;
		sz = size.yx; sz2 = size2.yx; d = delta.yx;
	}
	const float v = (abs(V) >> 1) + (V < -1 ? +d.y : V > +1 ? -d.y : abs(d.y));

	float u;
	if (v > sz.y) u = -1;
	else if (v > 0.5) {
		const float rel_v = (v - 0.5) / sz2.y;
		const float q = rel_v <= 1 ? (1 - pow(abs(rel_v), sup_ell_exp)) : 0;
		u = sz2.x * pow(abs(q), 1 / sup_ell_exp) + 0.5;
	}
	else u = (sz.x * sz.y - v * sz.x) / sz.y;
	arc[id.x] = u >= 0 ? int2(ceil(d.x - u), floor(d.x + u)) : int2(1, -1);
}
)"; // superellipse with exp < 1 and rx + ry - 2 * rx * ry < 0.
struct cs_cbuff_arc {
	float size_x, size_y, delta_x, delta_y;
	uint32_t size_arc_x, size_arc_y;
	float sup_ell_exp;

	[[maybe_unused]] uint8_t _pad[4];
};
static_assert(sizeof(cs_cbuff_arc) % 16 == 0);


////////////////////////////////
// Resource managements.
////////////////////////////////
constinit AviUtl2::finalizing::helpers::init_state init_state{};
D3D::ComPtr<::ID3D11ComputeShader>
	cs_arc_L2, cs_arc_L1, cs_arc_box,
	cs_arc_cross, cs_arc_Lp, cs_arc_cusp;

void quit()
{
	common::cs_bin_pass_1.Reset();
	cs_arc_L2.Reset();
	cs_arc_L1.Reset();
	cs_arc_box.Reset();
	cs_arc_cross.Reset();
	cs_arc_Lp.Reset();
	cs_arc_cusp.Reset();

	init_state.clear();
}
ANON_NS_E;


////////////////////////////////
// Exported functions.
////////////////////////////////
bool common::init()
{
	// assumes D3D is already initialized.
	init_state.init(&quit, [] {
		return

		#define cs_src(name)	cs_src_##name, "common::cs_" #name
			(common::cs_bin_pass_1 = D3D::create_compute_shader(cs_src(pass_1))) != nullptr &&
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

void common::buff_spec::prepare_arc(double radius_x, double radius_y, double superellipse_exp,
	double delta_x, double delta_y, uint32_t arc_width, uint32_t arc_height,
	::ID3D11UnorderedAccessView* uav_arc)
{
	// choose the shader.
	::ID3D11ComputeShader* cs_arc;
	if (superellipse_exp == 2) [[likely]] cs_arc = cs_arc_L2.Get();
	else if (std::isinf(superellipse_exp)) cs_arc = cs_arc_box.Get();
	else if (superellipse_exp <= 0) cs_arc = cs_arc_cross.Get();
	else if (superellipse_exp > 1)  cs_arc = cs_arc_Lp.Get();
	else if (superellipse_exp == 1 || radius_x + radius_y - 2 * radius_x * radius_y >= 0)
		cs_arc = cs_arc_L1.Get();
	else cs_arc = cs_arc_cusp.Get();

	// prepare cbuffer.
	auto cbuff = D3D::create_const_buffer(cs_cbuff_arc{
		.size_x = static_cast<float>(radius_x), .size_y = static_cast<float>(radius_y),
		.delta_x = static_cast<float>(delta_x), .delta_y = static_cast<float>(delta_y),
		.size_arc_x = arc_width, .size_arc_y = arc_height,
		.sup_ell_exp = static_cast<float>(superellipse_exp),
	});

	// execute shader.
	D3D::cxt->CSSetShader(cs_arc, nullptr, 0);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_arc, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff.GetAddressOf());
	D3D::cxt->Dispatch((arc_width + arc_height + ((1 << 6) - 1)) >> 6, 1, 1);

	// cleanup.
	D3D::cxt->ClearState();
}

uint32_t common::buff_spec::stride_mid(int width_src, int height_src, int width_dst, int height_dst)
{
	static_assert(D3D::buffer_stride_align % elem_size_mid == 0);
	constexpr uint32_t unit_align = D3D::buffer_stride_align / elem_size_mid;

	uint32_t const width = static_cast<uint32_t>(width_src);
	return (width + (unit_align - 1)) & (0u - unit_align);
}

void common::buff_spec::get_size_mid(int width_src, int height_src, int width_dst, int height_dst, uint32_t& length)
{
	uint32_t const
		stride = stride_mid(width_src, height_src, width_dst, height_dst),
		height = static_cast<uint32_t>(height_dst);
	length = elem_size_mid * stride * height;
}
