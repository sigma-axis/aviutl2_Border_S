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

#include "method_bin.hpp"
using bin = Border_S::method::bin;
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
StructuredBuffer<int2> arc : register(t1);
cbuffer constant0 : register(b0) {
	int2 size_mid, size_dst;
	int2 range_src_y, range_mid_x;
	int2 delta;
	uint2 size_arc;
	uint stride_mid;
	float thresh, alpha_base;
};
static const uint L = size_arc.y >> 1;
[numthreads(1, 64, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if ((id >= uint2(size_dst)).y) return;

	const uint
		mid_idx_base = stride_mid * id.y - uint(range_mid_x[0]),
		outer_l = alpha_base > 0 ? 0 : ~0u;

	int pad = -1;
	for (int x = min(0, range_mid_x[0]); x < size_dst.x; x++, pad--) {
		const uint2 l = (range_mid_x[0] <= x && x < range_mid_x[1]) ?
			mid[uint(x) + mid_idx_base] : outer_l;
		const int2 k = l <= L ? int2(
			arc[L + l.x].y,
			arc[L - l.y].y) : -1;
		pad = max(pad, max(k.x, k.y));
		if (x >= 0) {
			const float v = pad >= 0 ? 1 : 0;
			dst[uint2(x, id.y)] = v;
		}
	}
	pad = -1;
	for (x = max(size_dst.x, range_mid_x[1]); --x >= 0; pad--) {
		const uint2 l = (range_mid_x[0] <= x && x < range_mid_x[1]) ?
			mid[uint(x) + mid_idx_base] : outer_l;
		const int2 k = l <= L ? int2(
			-arc[L + l.x].x,
			-arc[L - l.y].x) : -1;
		pad = max(pad, max(k.x, k.y));
		if (x < size_dst.x) {
			const float v = pad >= 0 ? 1 : 0;
			dst[uint2(x, id.y)] = abs(max(v, dst[uint2(x, id.y)]) - alpha_base);
		}
	}
}
)";


////////////////////////////////
// Resource managements.
////////////////////////////////
constinit AviUtl2::finalizing::helpers::init_state init_state{};
D3D::ComPtr<::ID3D11ComputeShader> cs_pass_2;
void quit()
{
	cs_pass_2.Reset();

	init_state.clear();
}
bool init()
{
	// assumes D3D is already initialized.
	init_state.init(&quit, [] {
		return

		#define cs_src(name)	cs_src_##name, "bin::cs_" #name
			(cs_pass_2 = D3D::create_compute_shader(cs_src(pass_2))) != nullptr &&
		#undef cs_src

			true;
	});
	return init_state;
}
ANON_NS_E;


////////////////////////////////
// Exported functions.
////////////////////////////////
bool bin::inflate(
	bool deflation,
	int width_src, int height_src,
	int width_dst, int height_dst,
	int offset_x, int offset_y, double delta_x, double delta_y,
	::ID3D11ShaderResourceView* srv_src, ::ID3D11UnorderedAccessView* uav_shape,
	double threshold, double radius_x, double radius_y, double superellipse_exp,
	D3D::cs_views const& arc, D3D::cs_views const& mid)
{
	if (!init() || !common::init()) return false;

	uint32_t arc_length; bin::buff_spec::get_size_arc(radius_x, radius_y, arc_length);
	arc_length /= bin::buff_spec::elem_size_arc;

	// prepare the arc buffer.
	common::buff_spec::prepare_arc(radius_x, radius_y, superellipse_exp,
		delta_x, delta_y, 0, arc_length, arc.uav);

	// create constant buffer.
	auto cbuff = D3D::create_const_buffer(common::cs_cbuff_bin_inf_def{
		.size_mid_x = width_src, .size_mid_y = height_dst,
		.size_dst_x = width_dst, .size_dst_y = height_dst,
		.range_src_t = offset_y, .range_src_b = offset_y + height_src,
		.range_mid_l = offset_x, .range_mid_r = offset_x + width_src,
		.delta_x = 0, .delta_y = 0,
		.size_arc_x = 0, .size_arc_y = arc_length,
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
	::ID3D11ShaderResourceView* const srv_pass_2[] = { mid.srv, arc.srv };
	D3D::cxt->CSSetShaderResources(0, std::size(srv_pass_2), srv_pass_2); // ... so `mid.srv` can be bound here.
	D3D::cxt->Dispatch(1, (height_dst + ((1 << 6) - 1)) >> 6, 1);

	// cleanup.
	D3D::cxt->ClearState();

	return true;
}

void bin::buff_spec::get_size_arc(double radius_x, double radius_y, uint32_t& length)
{
	uint32_t const count = 2 * static_cast<uint32_t>(std::ceil(radius_y)) + 1;
	length = elem_size_arc * count;

	static_assert(elem_size_arc == common::buff_spec::elem_size_arc);
}

void bin::buff_spec::get_size_mid(int width_src, int height_src, int width_dst, int height_dst, uint32_t& length)
{
	common::buff_spec::get_size_mid(width_src, height_src, width_dst, height_dst, length);

	static_assert(elem_size_mid == common::buff_spec::elem_size_mid);
}
