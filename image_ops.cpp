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

#include "image_ops.hpp"
using Border_S::image_ops::color_float;
using Border_S::image_ops::pattern_info;
using Border_S::image_ops::ops;

#define ANON_NS_B namespace {
#define ANON_NS_E }


ANON_NS_B;
////////////////////////////////
// structures for shaders.
////////////////////////////////
struct mat2x2_move_float {
	float a11 = 0, a21 = 0;
	[[maybe_unused]] float _pad31 = 0, _pad32 = 0;
	float a12 = 0, a22 = 0;
	float x = 0, y = 0;

	constexpr mat2x2_move_float() = default;
	constexpr mat2x2_move_float(pattern_info const& pattern, double center_src_x, double center_src_y)
	{
		double const
			c = std::cos(-pattern.rotate) / pattern.scale,
			s = std::sin(-pattern.rotate) / pattern.scale;
		a11 = static_cast<float>(c / pattern.width); a12 = -static_cast<float>(s / pattern.width);
		a21 = static_cast<float>(s / pattern.height); a22 = static_cast<float>(c / pattern.height);

		double const
			pre_x = -(center_src_x + pattern.pos_x),
			pre_y = -(center_src_y + pattern.pos_y);
		x = static_cast<float>((c * pre_x - s * pre_y) / pattern.width + 0.5);
		y = static_cast<float>((s * pre_x + c * pre_y) / pattern.height + 0.5);
	}
};

////////////////////////////////
// shaders.
////////////////////////////////
constexpr char cs_src_extract_alpha[] = R"(
RWTexture2D<float> dst : register(u0);
Texture2D<half4> src : register(t0);
cbuffer constant0 : register(b0) {
	uint2 size_dst;
	uint2 ofs_src;
	uint2 ofs_dst;
};
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_dst)) return;
	dst[id + ofs_dst] = saturate(src[id + ofs_src].a);
}
)";
struct cs_cbuff_extract_alpha {
	uint32_t size_dst_x, size_dst_y;
	uint32_t ofs_src_x, ofs_src_y;
	uint32_t ofs_dst_x, ofs_dst_y;

	uint8_t _pad[8];
};
static_assert(sizeof(cs_cbuff_extract_alpha) % 16 == 0);

constexpr char cs_src_draw[] = R"(
RWTexture2D<half4> dst : register(u0);
Texture2D<half4> src : register(t0);
Texture2D<float> shape : register(t1);
cbuffer constant0 : register(b0) {
	uint2 size_src;
	uint2 size_dst;
	float4 color;
	int2 offset;
	float a_front, a_back;
};
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_dst)) return;

	const uint2 pos_src = id - uint2(offset);
	const float4 col = all(pos_src < size_src) ? src[pos_src] : 0;
	dst[id] = a_front * col + (1 - col.a) * a_back * shape[id] * color;
}
)";
struct cs_cbuff_draw {
	uint32_t size_src_x, size_src_y;
	uint32_t size_dst_x, size_dst_y;
	color_float color;
	int32_t offset_x, offset_y;
	float a_front, a_back;
};
static_assert(sizeof(cs_cbuff_draw) % 16 == 0);

constexpr char cs_src_draw_pat[] = R"(
RWTexture2D<half4> dst : register(u0);
Texture2D<half4> src : register(t0);
Texture2D<float> shape : register(t1);
Texture2D<half4> pat : register(t2);
SamplerState smp : register(s0);
cbuffer constant0 : register(b0) {
	uint2 size_src;
	uint2 size_dst;
	int2 offset;
	float a_front, a_back;
	float2x2 mat_pat;
	float2 offset_pat;
};
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_dst)) return;

	const uint2 pos_src = id - uint2(offset);
	const float4 col = all(pos_src < size_src) ? src[pos_src] : 0,
		bak = pat.SampleGrad(smp, mul(mat_pat, id + (0.5 - 1 / 4096.0)) + offset_pat,
			mat_pat._11_21, mat_pat._12_22);
	dst[id] = a_front * col + (1 - col.a) * a_back * shape[id] * bak;
}
)";
struct cs_cbuff_draw_pat {
	uint32_t size_src_x, size_src_y;
	uint32_t size_dst_x, size_dst_y;
	int32_t offset_x, offset_y;
	float a_front, a_back;
	mat2x2_move_float mat_move_pat;
};
static_assert(sizeof(cs_cbuff_draw_pat) % 16 == 0);

constexpr char cs_src_recolor[] = R"(
RWTexture2D<half4> dst : register(u0);
Texture2D<float> shape : register(t0);
cbuffer constant0 : register(b0) {
	uint2 size_shape;
	uint2 size_dst;
	float4 color;
	int2 offset;
	float a_front, a_back;
	float alpha_base;
};
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_dst)) return;

	const uint2 pos_shape = id - uint2(offset);
	const float alpha = a_front * abs((all(pos_shape < size_shape) ?
		shape[pos_shape] : 0) - alpha_base);
	const float4 col = dst[id];
	dst[id] = col.a * alpha * color + (1 - alpha * color.a) * a_back * col;
}
)";
struct cs_cbuff_recolor {
	uint32_t size_shape_x, size_shape_y;
	uint32_t size_dst_x, size_dst_y;
	color_float color;
	int32_t offset_x, offset_y;
	float a_front, a_back;
	float alpha_base;

	[[maybe_unused]] uint8_t _pad[12];
};
static_assert(sizeof(cs_cbuff_recolor) % 16 == 0);

constexpr char cs_src_recolor_pat[] = R"(
RWTexture2D<half4> dst : register(u0);
Texture2D<float> shape : register(t0);
Texture2D<half4> pat : register(t1);
SamplerState smp : register(s0);
cbuffer constant0 : register(b0) {
	uint2 size_shape;
	uint2 size_dst;
	int2 offset;
	float a_front, a_back;
	float2x2 mat_pat;
	float2 offset_pat;
	float alpha_base;
};
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_dst)) return;

	const uint2 pos_shape = id - uint2(offset);
	const float alpha = a_front * abs((all(pos_shape < size_shape) ?
		shape[pos_shape] : 0) - alpha_base);
	const float4 col = dst[id],
		over = pat.SampleGrad(smp, mul(mat_pat, id + (0.5 - 1 / 4096.0)) + offset_pat,
			mat_pat._11_21, mat_pat._12_22);
	dst[id] = col.a * alpha * over + (1 - alpha * over.a) * a_back * col;
}
)";
struct cs_cbuff_recolor_pat {
	uint32_t size_shape_x, size_shape_y;
	uint32_t size_dst_x, size_dst_y;
	int32_t offset_x, offset_y;
	float a_front, a_back;
	mat2x2_move_float mat_move_pat;
	float alpha_base;

	[[maybe_unused]] uint8_t _pad[12];
};
static_assert(sizeof(cs_cbuff_recolor_pat) % 16 == 0);

constexpr char cs_src_recolor_empty[] = R"(
RWTexture2D<half4> dst : register(u0);
cbuffer constant0 : register(b0) {
	float4 color;
	uint2 size_dst;
	float a_front, a_back;
};
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_dst)) return;

	const float4 col = dst[id];
	dst[id] = col.a * a_front * color + (1 - a_front * color.a) * a_back * col;
}
)";
struct cs_cbuff_recolor_emtpy {
	color_float color;
	uint32_t size_dst_x, size_dst_y;
	float a_front, a_back;
};
static_assert(sizeof(cs_cbuff_recolor_emtpy) % 16 == 0);

constexpr char cs_src_recolor_empty_pat[] = R"(
RWTexture2D<half4> dst : register(u0);
Texture2D<half4> pat : register(t0);
SamplerState smp : register(s0);
cbuffer constant0 : register(b0) {
	uint2 size_dst;
	float a_front, a_back;
	float2x2 mat_pat;
	float2 offset_pat;
};
uint2 get_pat_size()
{
	uint w, h;
	pat.GetDimensions(w, h);
	return uint2(w, h);
}
static const float2 inv_size_pat = 1.0 / get_pat_size();
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_dst)) return;

	const float4 col = dst[id],
		over = pat.SampleGrad(smp, mul(mat_pat, id + (0.5 - 1 / 4096.0)) + offset_pat,
			mat_pat._11_21, mat_pat._12_22);
	dst[id] = col.a * a_front * over + (1 - a_front * over.a) * a_back * col;
}
)";
struct cs_cbuff_recolor_emtpy_pat {
	uint32_t size_dst_x, size_dst_y;
	float a_front, a_back;
	mat2x2_move_float mat_move_pat;
};
static_assert(sizeof(cs_cbuff_recolor_emtpy_pat) % 16 == 0);

constexpr char cs_src_carve[] = R"(
RWTexture2D<half4> dst : register(u0);
Texture2D<float> src : register(t0);
cbuffer constant0 : register(b0) {
	uint2 size_src;
	uint2 size_dst;
	int2 offset;
	float2 alpha;
};
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_dst)) return;

	const uint2 pos_src = id - uint2(offset);
	const float a = all(pos_src < size_src) ? src[pos_src] : 0;
	dst[id] *= lerp(alpha[0], alpha[1], a);
}
)";
constexpr char cs_src_carve_1[] = R"(
RWTexture2D<float> dst : register(u0);
Texture2D<float> src : register(t0);
cbuffer constant0 : register(b0) {
	uint2 size_src;
	uint2 size_dst;
	int2 offset;
	float2 alpha;
};
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_dst)) return;

	const uint2 pos_src = id - uint2(offset);
	const float a = all(pos_src < size_src) ? src[pos_src] : 0;
	dst[id] *= lerp(alpha[0], alpha[1], a);
}
)";
struct cs_cbuff_carve {
	uint32_t size_src_x, size_src_y;
	uint32_t size_dst_x, size_dst_y;
	int32_t offset_x, offset_y;
	float alpha_min, alpha_max;
};
static_assert(sizeof(cs_cbuff_carve) % 16 == 0);

constexpr char cs_src_combine[] = R"(
RWTexture2D<half4> dst : register(u0);
Texture2D<half4> src : register(t0);
Texture2D<float> shape : register(t1);
cbuffer constant0 : register(b0) {
	uint2 size_src, size_shape;
	uint2 size_dst;
	bool is_src_front;
	float4 color;
	int2 offset_src, offset_shape;
	float a_src, a_shape;
};
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_dst)) return;

	const uint2 pos_src = id - uint2(offset_src);
	const uint2 pos_shape = id - uint2(offset_shape);
	float4 col_f = a_src * (all(pos_src < size_src) ? src[pos_src] : 0);
	float4 col_b = a_shape * (all(pos_shape < size_shape) ? shape[pos_shape] : 0) * color;
	if (!is_src_front) {
		float4 c = col_f; col_f = col_b; col_b = c;
	}
	dst[id] = col_f + (1 - col_f.a) * col_b;
}
)";
struct cs_cbuff_combine {
	uint32_t size_src_x, size_src_y;
	uint32_t size_shape_x, size_shape_y;
	uint32_t size_dst_x, size_dst_y;
	bool is_src_front; [[maybe_unused]] uint8_t _pad7[7];
	color_float color;
	int32_t offset_src_x, offset_src_y;
	int32_t offset_shape_x, offset_shape_y;
	float a_src, a_shape;

	[[maybe_unused]] uint8_t _pad[8];
};
static_assert(sizeof(cs_cbuff_combine) % 16 == 0);

constexpr char cs_src_combine_pat[] = R"(
RWTexture2D<half4> dst : register(u0);
Texture2D<half4> src : register(t0);
Texture2D<float> shape : register(t1);
Texture2D<half4> pat : register(t2);
SamplerState smp : register(s0);
cbuffer constant0 : register(b0) {
	uint2 size_src, size_shape;
	uint2 size_dst;
	bool is_src_front;
	int2 offset_src, offset_shape;
	float a_src, a_shape;
	float2x2 mat_pat;
	float2 offset_pat;
};
uint2 get_pat_size()
{
	uint w, h;
	pat.GetDimensions(w, h);
	return uint2(w, h);
}
static const float2 inv_size_pat = 1.0 / get_pat_size();
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_dst)) return;

	const uint2 pos_src = id - uint2(offset_src);
	const uint2 pos_shape = id - uint2(offset_shape);
	float4 col_f = a_src * (all(pos_src < size_src) ? src[pos_src] : 0);
	float4 col_b = a_shape * (all(pos_shape < size_shape) ? shape[pos_shape] : 0)
		* pat.SampleGrad(smp, mul(mat_pat, id + (0.5 - 1 / 4096.0)) + offset_pat,
			mat_pat._11_21, mat_pat._12_22);
	if (!is_src_front) {
		float4 c = col_f; col_f = col_b; col_b = c;
	}
	dst[id] = col_f + (1 - col_f.a) * col_b;
}
)";
struct cs_cbuff_combine_pat {
	uint32_t size_src_x, size_src_y;
	uint32_t size_shape_x, size_shape_y;
	uint32_t size_dst_x, size_dst_y;
	bool is_src_front; [[maybe_unused]] uint8_t _pad7[7];
	int32_t offset_src_x, offset_src_y;
	int32_t offset_shape_x, offset_shape_y;
	float a_src, a_shape;

	[[maybe_unused]] uint8_t _pad[8];

	mat2x2_move_float mat_move_pat;
};
static_assert(sizeof(cs_cbuff_combine_pat) % 16 == 0);

// assumes size_src.x >= span_i + 1.
constexpr char cs_src_blur_x1[] = R"(
RWTexture2D<float> dst : register(u0);
Texture2D<float> src : register(t0);
cbuffer constant0 : register(b0) {
	uint2 size_src;
	uint2 size_dst;
	uint span_i;
	float span_f, inv_span;
};
int quantize(float v)
{
	return int(round((1 << 16) * v));
}
[numthreads(1, 64, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if ((id >= size_dst).y) return;

	int sum = 0;
	for (uint x = 0; x < span_i; x++) {
		float a = src[uint2(x, id.y)];
		dst[uint2(size_dst.x - 1 - x, id.y)] = inv_span * (sum + span_f * a);
		sum += quantize(a);
	}
	for (; x < size_src.x; x++) {
		float a = src[uint2(x, id.y)], a0 = src[uint2(x - span_i, id.y)];
		dst[uint2(size_dst.x - 1 - x, id.y)] = inv_span * (sum + span_f * a);
		sum += quantize(a) - quantize(a0);
	}
	for (; x < size_dst.x; x++) {
		float a0 = src[uint2(x - span_i, id.y)];
		dst[uint2(size_dst.x - 1 - x, id.y)] = inv_span * sum;
		sum -= quantize(a0);
	}
}
)";
// assumes size_src.x <= span_i.
constexpr char cs_src_blur_x2[] = R"(
RWTexture2D<float> dst : register(u0);
Texture2D<float> src : register(t0);
cbuffer constant0 : register(b0) {
	uint2 size_src;
	uint2 size_dst;
	uint span_i;
	float span_f, inv_span;
};
int quantize(float v)
{
	return int(round((1 << 16) * v));
}
[numthreads(1, 64, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if ((id >= size_dst).y) return;

	int sum = 0;
	for (uint x = 0; x < size_src.x; x++) {
		float a = src[uint2(x, id.y)];
		dst[uint2(size_dst.x - 1 - x, id.y)] = inv_span * (sum + span_f * a);
		sum += quantize(a);
	}
	for (; x < span_i; x++)
		dst[uint2(size_dst.x - 1 - x, id.y)] = inv_span * sum;
	for (; x < size_dst.x; x++) {
		float a0 = src[uint2(x - span_i, id.y)];
		dst[uint2(size_dst.x - 1 - x, id.y)] = inv_span * sum;
		sum -= quantize(a0);
	}
}
)";
// assumes size_src.y >= span_i + 1.
constexpr char cs_src_blur_y1[] = R"(
RWTexture2D<float> dst : register(u0);
Texture2D<float> src : register(t0);
cbuffer constant0 : register(b0) {
	uint2 size_src;
	uint2 size_dst;
	uint span_i;
	float span_f, inv_span;
};
int quantize(float v)
{
	return int(round((1 << 16) * v));
}
[numthreads(64, 1, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if ((id >= size_dst).x) return;

	int sum = 0;
	for (uint y = 0; y < span_i; y++) {
		float a = src[uint2(id.x, y)];
		dst[uint2(id.x, size_dst.y - 1 - y)] = inv_span * (sum + span_f * a);
		sum += quantize(a);
	}
	for (; y < size_src.y; y++) {
		float a = src[uint2(id.x, y)], a0 = src[uint2(id.x, y - span_i)];
		dst[uint2(id.x, size_dst.y - 1 - y)] = inv_span * (sum + span_f * a);
		sum += quantize(a) - quantize(a0);
	}
	for (; y < size_dst.y; y++) {
		float a0 = src[uint2(id.x, y - span_i)];
		dst[uint2(id.x, size_dst.y - 1 - y)] = inv_span * sum;
		sum -= quantize(a0);
	}
}
)";
// assumes size_src.y <= span_i.
constexpr char cs_src_blur_y2[] = R"(
RWTexture2D<float> dst : register(u0);
Texture2D<float> src : register(t0);
cbuffer constant0 : register(b0) {
	uint2 size_src;
	uint2 size_dst;
	uint span_i;
	float span_f, inv_span;
};
int quantize(float v)
{
	return int(round((1 << 16) * v));
}
[numthreads(64, 1, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if ((id >= size_dst).x) return;

	int sum = 0;
	for (uint y = 0; y < size_src.y; y++) {
		float a = src[uint2(id.x, y)];
		dst[uint2(id.x, size_dst.y - 1 - y)] = inv_span * (sum + span_f * a);
		sum += quantize(a);
	}
	for (; y < span_i; y++)
		dst[uint2(id.x, size_dst.y - 1 - y)] = inv_span * sum;
	for (; y < size_dst.y; y++) {
		float a0 = src[uint2(id.x, y - span_i)];
		dst[uint2(id.x, size_dst.y - 1 - y)] = inv_span * sum;
		sum -= quantize(a0);
	}
}
)";
struct cs_cbuff_blur {
	uint32_t size_src_x, size_src_y;
	uint32_t size_dst_x, size_dst_y;
	uint32_t span_i;
	float span_f, inv_span;

	[[maybe_unused]] uint8_t _pad[4];

	static constexpr uint32_t quantize_denom = 1 << 16;
};
static_assert(sizeof(cs_cbuff_blur) % 16 == 0);

constexpr char cs_src_gauss_blur_x[] = R"(
RWTexture2D<float> dst : register(u0);
Texture2D<float> src : register(t0);
SamplerState smp : register(s0);
cbuffer constant0 : register(b0) {
	uint2 size_src;
	uint2 size_dst;
	float4 rates;
	uint span_i;
	float inv_span;
};
uint2 get_src_size()
{
	uint w, h;
	src.GetDimensions(w, h);
	return uint2(w, h);
}
static const float2 inv_size_src = 1.0 / get_src_size();
float pick(float2 pos)
{
	float mx = max(pos.x - size_src.x + 0.5, 0);
	return max(1 - mx, 0) * src.SampleLevel(smp, float2(pos.x - mx, pos.y) * inv_size_src, 0);
}

[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_dst)) return;

	const int base_x = id.x - span_i;
	float sum = src.Load(int3(base_x, id.y, 0)),
		dwt0 = 1, wt0 = rates[0], dwt01 = rates[1];
	for (uint x = 1; x <= span_i; x += 2, dwt0 *= rates[3], wt0 *= dwt0, dwt01 *= rates[2]) {
		if (x == span_i) dwt01 = 0;
		const float wt1 = wt0 * dwt01, med = saturate(1 - rcp(1 + dwt01)), x_med = x + med;
		const float src_l = base_x - x_med, src_r = base_x + x_med;
		sum += (wt0 + wt1) * (pick(float2(src_l, id.y) + 0.5) + pick(float2(src_r, id.y) + 0.5));
	}

	dst[id] = inv_span * sum;
}
)";
constexpr char cs_src_gauss_blur_y[] = R"(
RWTexture2D<float> dst : register(u0);
Texture2D<float> src : register(t0);
SamplerState smp : register(s0);
cbuffer constant0 : register(b0) {
	uint2 size_src;
	uint2 size_dst;
	float4 rates;
	uint span_i;
	float inv_span;
};
uint2 get_src_size()
{
	uint w, h;
	src.GetDimensions(w, h);
	return uint2(w, h);
}
static const float2 inv_size_src = 1.0 / get_src_size();
float pick(float2 pos)
{
	float my = max(pos.y - size_src.y + 0.5, 0);
	return max(1 - my, 0) * src.SampleLevel(smp, float2(pos.x, pos.y - my) * inv_size_src, 0);
}

[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_dst)) return;

	const int base_y = id.y - span_i;
	float sum = src.Load(int3(id.x, base_y, 0)),
		dwt0 = 1, wt0 = rates[0], dwt01 = rates[1];
	for (uint y = 1; y <= span_i; y += 2, dwt0 *= rates[3], wt0 *= dwt0, dwt01 *= rates[2]) {
		if (y == span_i) dwt01 = 0;
		const float wt1 = wt0 * dwt01, med = saturate(1 - rcp(1 + dwt01)), y_med = y + med;
		const float src_t = base_y - y_med, src_b = base_y + y_med;
		sum += (wt0 + wt1) * (pick(float2(id.x, src_t) + 0.5) + pick(float2(id.x, src_b) + 0.5));
	}

	dst[id] = inv_span * sum;
}
)";
struct cs_cbuff_gauss_blur {
	uint32_t size_src_x, size_src_y;
	uint32_t size_dst_x, size_dst_y;
	float rate1, rate3, rate4, rate8; // rate_k = exp(-k / (2 sigma^2))
	uint32_t span_i;
	float inv_span;

	[[maybe_unused]] uint8_t _pad[8];
};
static_assert(sizeof(cs_cbuff_gauss_blur) % 16 == 0);

constexpr char cs_src_delta_move[] = R"(
RWTexture2D<float> dst : register(u0);
Texture2D<float> src : register(t0);
SamplerState smp : register(s0);
cbuffer constant0 : register(b0) {
	uint2 size_dst;
	float2 delta;
};
uint2 get_src_size()
{
	uint w, h;
	src.GetDimensions(w, h);
	return uint2(w, h);
}
static const float2 inv_size_src = 1.0 / get_src_size();
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_dst)) return;

	const float2
		pos_src = float2(id) + 0.5 - delta,
		m = max(pos_src - size_dst + 0.5, 0), im = max(1 - m, 0);
	dst[id] = im.x * im.y * src.SampleLevel(smp, (pos_src - m) * inv_size_src, 0);
}
)";
struct cs_cbuff_delta_move {
	uint32_t size_dst_x, size_dst_y;
	float delta_x, delta_y;
};
static_assert(sizeof(cs_cbuff_delta_move) % 16 == 0);


////////////////////////////////
// Resource managements.
////////////////////////////////
constinit AviUtl2::finalizing::helpers::init_state init_state{};
D3D::ComPtr<::ID3D11ComputeShader> cs_extract_alpha,
	cs_draw, cs_draw_pat, cs_recolor, cs_recolor_pat, cs_recolor_empty, cs_recolor_empty_pat,
	cs_carve, cs_carve_1, cs_combine, cs_combine_pat,
	cs_blur_x1, cs_blur_x2, cs_blur_y1, cs_blur_y2,
	cs_gauss_blur_x, cs_gauss_blur_y, cs_delta_move;
void quit()
{
	cs_extract_alpha.Reset();
	cs_draw.Reset();
	cs_draw_pat.Reset();
	cs_recolor.Reset();
	cs_recolor_pat.Reset();
	cs_recolor_empty.Reset();
	cs_recolor_empty_pat.Reset();
	cs_carve.Reset();
	cs_carve_1.Reset();
	cs_combine.Reset();
	cs_combine_pat.Reset();
	cs_blur_x1.Reset();
	cs_blur_x2.Reset();
	cs_blur_y1.Reset();
	cs_blur_y2.Reset();
	cs_gauss_blur_x.Reset();
	cs_gauss_blur_y.Reset();
	cs_delta_move.Reset();

	init_state.clear();
}
bool init()
{
	// assumes D3D is already initialized.
	init_state.init(&quit, [] {
		return

		#define cs_src(name)	cs_src_##name, "image_ops::cs_" #name
			(cs_extract_alpha = D3D::create_compute_shader(cs_src(extract_alpha))) != nullptr &&
			(cs_draw = D3D::create_compute_shader(cs_src(draw))) != nullptr &&
			(cs_draw_pat = D3D::create_compute_shader(cs_src(draw_pat))) != nullptr &&
			(cs_recolor = D3D::create_compute_shader(cs_src(recolor))) != nullptr &&
			(cs_recolor_pat = D3D::create_compute_shader(cs_src(recolor_pat))) != nullptr &&
			(cs_recolor_empty = D3D::create_compute_shader(cs_src(recolor_empty))) != nullptr &&
			(cs_recolor_empty_pat = D3D::create_compute_shader(cs_src(recolor_empty_pat))) != nullptr &&
			(cs_carve = D3D::create_compute_shader(cs_src(carve))) != nullptr &&
			(cs_carve_1 = D3D::create_compute_shader(cs_src(carve_1))) != nullptr &&
			(cs_combine = D3D::create_compute_shader(cs_src(combine))) != nullptr &&
			(cs_combine_pat = D3D::create_compute_shader(cs_src(combine_pat))) != nullptr &&
			(cs_blur_x1 = D3D::create_compute_shader(cs_src(blur_x1))) != nullptr &&
			(cs_blur_x2 = D3D::create_compute_shader(cs_src(blur_x2))) != nullptr &&
			(cs_blur_y1 = D3D::create_compute_shader(cs_src(blur_y1))) != nullptr &&
			(cs_blur_y2 = D3D::create_compute_shader(cs_src(blur_y2))) != nullptr &&
			(cs_gauss_blur_x = D3D::create_compute_shader(cs_src(gauss_blur_x))) != nullptr &&
			(cs_gauss_blur_y = D3D::create_compute_shader(cs_src(gauss_blur_y))) != nullptr &&
			(cs_delta_move = D3D::create_compute_shader(cs_src(delta_move))) != nullptr &&
		#undef cs_src

			true;
	});
	return init_state;
}
ANON_NS_E


////////////////////////////////
// Implementations of ops.
////////////////////////////////
bool ops::extract_alpha(
	int width, int height,
	int src_left, int src_top, ::ID3D11ShaderResourceView* src,
	int dst_left, int dst_top, ::ID3D11UnorderedAccessView* dst)
{
	if (!init()) return false;

	// create constant buffer.
	auto cbuff = D3D::create_const_buffer(cs_cbuff_extract_alpha{
		.size_dst_x = static_cast<uint32_t>(width), .size_dst_y = static_cast<uint32_t>(height),
		.ofs_src_x = static_cast<uint32_t>(src_left), .ofs_src_y = static_cast<uint32_t>(src_top),
		.ofs_dst_x = static_cast<uint32_t>(dst_left), .ofs_dst_y = static_cast<uint32_t>(dst_top),
	});
	if (cbuff == nullptr) return false;

	// execute shader.
	D3D::cxt->CSSetShader(cs_extract_alpha.Get(), nullptr, 0);
	D3D::cxt->CSSetShaderResources(0, 1, &src);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &dst, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff.GetAddressOf());
	D3D::cxt->Dispatch(
		(width + ((1 << 3) - 1)) >> 3,
		(height + ((1 << 3) - 1)) >> 3, 1);

	// cleanup.
	D3D::cxt->ClearState();

	return true;
}

bool ops::draw(
	int width_src, int height_src,
	int width_dst, int height_dst,
	int offset_x, int offset_y,
	::ID3D11ShaderResourceView* srv_src,
	::ID3D11ShaderResourceView* srv_shape, ::ID3D11UnorderedAccessView* uav_dst,
	pattern_info const& pattern,
	double alpha_front, double alpha_back)
{
	if (!init()) return false;

	// create constant buffer.
	auto cbuff = pattern.is_color() ? D3D::create_const_buffer(cs_cbuff_draw{
		.size_src_x = static_cast<uint32_t>(width_src), .size_src_y = static_cast<uint32_t>(height_src),
		.size_dst_x = static_cast<uint32_t>(width_dst), .size_dst_y = static_cast<uint32_t>(height_dst),
		.color = pattern.solid,
		.offset_x = offset_x, .offset_y = offset_y,
		.a_front = static_cast<float>(alpha_front),
		.a_back = static_cast<float>(alpha_back),
	}) : D3D::create_const_buffer(cs_cbuff_draw_pat{
		.size_src_x = static_cast<uint32_t>(width_src), .size_src_y = static_cast<uint32_t>(height_src),
		.size_dst_x = static_cast<uint32_t>(width_dst), .size_dst_y = static_cast<uint32_t>(height_dst),
		.offset_x = offset_x, .offset_y = offset_y,
		.a_front = static_cast<float>(alpha_front),
		.a_back = static_cast<float>(alpha_back),
		.mat_move_pat = { pattern, width_src / 2.0 + offset_x, height_src / 2.0 + offset_y },
	});
	if (cbuff == nullptr) return false;

	// execute shader.
	D3D::cxt->CSSetShader(pattern.is_color() ? cs_draw.Get() : cs_draw_pat.Get(), nullptr, 0);
	::ID3D11ShaderResourceView* const srv_draw[] = { srv_src, srv_shape, pattern.srv };
	D3D::cxt->CSSetShaderResources(0, std::size(srv_draw), srv_draw);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_dst, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff.GetAddressOf());
	if (pattern.is_pattern()) {
		auto smp = D3D::create_sampler_state(
			pattern.snap_to_pixel ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			D3D11_TEXTURE_ADDRESS_WRAP);
		if (smp == nullptr) return false;
		D3D::cxt->CSSetSamplers(0, 1, smp.GetAddressOf());
	}
	D3D::cxt->Dispatch(
		(width_dst + ((1 << 3) - 1)) >> 3,
		(height_dst + ((1 << 3) - 1)) >> 3, 1);

	// cleanup.
	D3D::cxt->ClearState();

	return true;
}

bool ops::recolor(
	int width_shape, int height_shape,
	int width_dst, int height_dst,
	int offset_x, int offset_y,
	::ID3D11ShaderResourceView* srv_shape, ::ID3D11UnorderedAccessView* uav_dst,
	pattern_info const& pattern, bool invert,
	double alpha_front, double alpha_back)
{
	if (!init()) return false;

	if (width_shape > 0 && height_shape > 0 && srv_shape != nullptr) {
		// create constant buffer.
		auto cbuff = pattern.is_color() ? D3D::create_const_buffer(cs_cbuff_recolor{
			.size_shape_x = static_cast<uint32_t>(width_shape), .size_shape_y = static_cast<uint32_t>(height_shape),
			.size_dst_x = static_cast<uint32_t>(width_dst), .size_dst_y = static_cast<uint32_t>(height_dst),
			.color = pattern.solid,
			.offset_x = offset_x, .offset_y = offset_y,
			.a_front = static_cast<float>(alpha_front),
			.a_back = static_cast<float>(alpha_back),
			.alpha_base = invert ? 1.0f : 0.0f,
		}) : D3D::create_const_buffer(cs_cbuff_recolor_pat{
			.size_shape_x = static_cast<uint32_t>(width_shape), .size_shape_y = static_cast<uint32_t>(height_shape),
			.size_dst_x = static_cast<uint32_t>(width_dst), .size_dst_y = static_cast<uint32_t>(height_dst),
			.offset_x = offset_x, .offset_y = offset_y,
			.a_front = static_cast<float>(alpha_front),
			.a_back = static_cast<float>(alpha_back),
			.mat_move_pat = { pattern, width_dst / 2.0, height_dst / 2.0 },
			.alpha_base = invert ? 1.0f : 0.0f,
		});
		if (cbuff == nullptr) return false;

		// execute shader.
		D3D::cxt->CSSetShader(pattern.is_color() ? cs_recolor.Get() : cs_recolor_pat.Get(), nullptr, 0);
		::ID3D11ShaderResourceView* const srv_recolor[] = { srv_shape, pattern.srv };
		D3D::cxt->CSSetShaderResources(0, std::size(srv_recolor), srv_recolor);
		D3D::cxt->CSSetConstantBuffers(0, 1, cbuff.GetAddressOf());
	}
	else {
		// shape is recognized as empty.

		// create constant buffer.
		auto cbuff = pattern.is_color() ? D3D::create_const_buffer(cs_cbuff_recolor_emtpy{
			.color = pattern.solid,
			.size_dst_x = static_cast<uint32_t>(width_dst), .size_dst_y = static_cast<uint32_t>(height_dst),
			.a_front = invert ? static_cast<float>(alpha_front) : 0,
			.a_back = static_cast<float>(alpha_back),
		}) : D3D::create_const_buffer(cs_cbuff_recolor_emtpy_pat{
			.size_dst_x = static_cast<uint32_t>(width_dst), .size_dst_y = static_cast<uint32_t>(height_dst),
			.a_front = invert ? static_cast<float>(alpha_front) : 0,
			.a_back = static_cast<float>(alpha_back),
			.mat_move_pat = { pattern, width_dst / 2.0, height_dst / 2.0 },
		});
		if (cbuff == nullptr) return false;

		// execute shader.
		D3D::cxt->CSSetShader(pattern.is_color() ? cs_recolor_empty.Get() : cs_recolor_empty_pat.Get(), nullptr, 0);
		if (pattern.is_pattern())
			D3D::cxt->CSSetShaderResources(0, 1, &pattern.srv);
		D3D::cxt->CSSetConstantBuffers(0, 1, cbuff.GetAddressOf());
	}

	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_dst, nullptr);
	if (pattern.is_pattern()) {
		auto smp = D3D::create_sampler_state(
			pattern.snap_to_pixel ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			D3D11_TEXTURE_ADDRESS_WRAP);
		if (smp == nullptr) return false;
		D3D::cxt->CSSetSamplers(0, 1, smp.GetAddressOf());
	}
	D3D::cxt->Dispatch(
		(width_dst + ((1 << 3) - 1)) >> 3,
		(height_dst + ((1 << 3) - 1)) >> 3, 1);

	// cleanup.
	D3D::cxt->ClearState();

	return true;
}

bool ops::carve(
	int width_shape, int height_shape,
	int width_dst, int height_dst,
	int offset_x, int offset_y,
	::ID3D11ShaderResourceView* srv_shape, ::ID3D11UnorderedAccessView* uav_dst,
	double alpha, bool is_dst_scalar)
{
	if (alpha == 0) return true; // nothing to do.

	if (!init()) return false;

	// create constant buffer.
	auto cbuff = D3D::create_const_buffer(cs_cbuff_carve{
		.size_src_x = static_cast<uint32_t>(width_shape), .size_src_y = static_cast<uint32_t>(height_shape),
		.size_dst_x = static_cast<uint32_t>(width_dst), .size_dst_y = static_cast<uint32_t>(height_dst),
		.offset_x = offset_x, .offset_y = offset_y,
		.alpha_min = static_cast<float>(std::min(1.0, 1 - alpha)),
		.alpha_max = static_cast<float>(std::min(1.0, 1 + alpha)),
	});
	if (cbuff == nullptr) return false;

	// execute shader.
	D3D::cxt->CSSetShader(is_dst_scalar ? cs_carve_1.Get() : cs_carve.Get(), nullptr, 0);
	D3D::cxt->CSSetShaderResources(0, 1, &srv_shape);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_dst, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff.GetAddressOf());
	D3D::cxt->Dispatch(
		(width_dst + ((1 << 3) - 1)) >> 3,
		(height_dst + ((1 << 3) - 1)) >> 3, 1);

	// cleanup.
	D3D::cxt->ClearState();

	return true;
}

bool ops::combine(
	int width_src, int height_src,
	int width_shape, int height_shape,
	int width_dst, int height_dst,
	int offset_src_x, int offset_src_y, // offset of source within dest.
	int offset_shape_x, int offset_shape_y, // offset of shape within dest.
	::ID3D11ShaderResourceView* srv_src, ::ID3D11ShaderResourceView* srv_shape,
	::ID3D11UnorderedAccessView* uav_dst,
	pattern_info const& pattern,
	double alpha_src, double alpha_shape, bool is_src_front)
{
	if (!init()) return false;

	// create constant buffer.
	auto cbuff = pattern.is_color() ? D3D::create_const_buffer(cs_cbuff_combine{
		.size_src_x = static_cast<uint32_t>(width_src), .size_src_y = static_cast<uint32_t>(height_src),
		.size_shape_x = static_cast<uint32_t>(width_shape), .size_shape_y = static_cast<uint32_t>(height_shape),
		.size_dst_x = static_cast<uint32_t>(width_dst), .size_dst_y = static_cast<uint32_t>(height_dst),
		.is_src_front = is_src_front,
		.color = pattern.solid,
		.offset_src_x = offset_src_x, .offset_src_y = offset_src_y,
		.offset_shape_x = offset_shape_x, .offset_shape_y = offset_shape_y,
		.a_src = static_cast<float>(alpha_src),
		.a_shape = static_cast<float>(alpha_shape),
	}) : D3D::create_const_buffer(cs_cbuff_combine_pat{
		.size_src_x = static_cast<uint32_t>(width_src), .size_src_y = static_cast<uint32_t>(height_src),
		.size_shape_x = static_cast<uint32_t>(width_shape), .size_shape_y = static_cast<uint32_t>(height_shape),
		.size_dst_x = static_cast<uint32_t>(width_dst), .size_dst_y = static_cast<uint32_t>(height_dst),
		.is_src_front = is_src_front,
		.offset_src_x = offset_src_x, .offset_src_y = offset_src_y,
		.offset_shape_x = offset_shape_x, .offset_shape_y = offset_shape_y,
		.a_src = static_cast<float>(alpha_src),
		.a_shape = static_cast<float>(alpha_shape),
		.mat_move_pat = {
			pattern,
			width_src / 2.0 + offset_src_x,
			height_src / 2.0 + offset_src_y,
		},
	});
	if (cbuff == nullptr) return false;

	// execute shader.
	D3D::cxt->CSSetShader(pattern.is_color() ? cs_combine.Get() : cs_combine_pat.Get(), nullptr, 0);
	::ID3D11ShaderResourceView* const srv_combine[] = { srv_src, srv_shape, pattern.srv };
	D3D::cxt->CSSetShaderResources(0, std::size(srv_combine), srv_combine);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_dst, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff.GetAddressOf());
	if (pattern.is_pattern()) {
		auto smp = D3D::create_sampler_state(
			pattern.snap_to_pixel ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			D3D11_TEXTURE_ADDRESS_WRAP);
		if (smp == nullptr) return false;
		D3D::cxt->CSSetSamplers(0, 1, smp.GetAddressOf());
	}
	D3D::cxt->Dispatch(
		(width_dst + ((1 << 3) - 1)) >> 3,
		(height_dst + ((1 << 3) - 1)) >> 3, 1);

	// cleanup.
	D3D::cxt->ClearState();

	return true;
}

ANON_NS_B
bool blur_triangular(int width_src, int height_src,
	D3D::cs_views const& src, D3D::cs_views const& tmp,
	double blur_half_x, double blur_half_y)
{
	int const
		blur_half_xi = static_cast<int>(std::ceil(blur_half_x)),
		blur_half_yi = static_cast<int>(std::ceil(blur_half_y));
	double const
		blur_xf = blur_half_x - (blur_half_xi - 1),
		blur_yf = blur_half_y - (blur_half_yi - 1);

	// create constant buffer.
	D3D::ComPtr<::ID3D11Buffer> cbuff[] = {
		D3D::create_const_buffer(cs_cbuff_blur{
			.size_src_x = static_cast<uint32_t>(width_src), .size_src_y = static_cast<uint32_t>(height_src),
			.size_dst_x = static_cast<uint32_t>(width_src), .size_dst_y = static_cast<uint32_t>(height_src + blur_half_yi),
			.span_i = static_cast<uint32_t>(blur_half_yi),
			.span_f = static_cast<float>(blur_yf * cs_cbuff_blur::quantize_denom),
			.inv_span = static_cast<float>(1 / ((blur_half_yi + blur_yf) * cs_cbuff_blur::quantize_denom)),
		}),
		D3D::create_const_buffer(cs_cbuff_blur{
			.size_src_x = static_cast<uint32_t>(width_src), .size_src_y = static_cast<uint32_t>(height_src + blur_half_yi),
			.size_dst_x = static_cast<uint32_t>(width_src), .size_dst_y = static_cast<uint32_t>(height_src + 2 * blur_half_yi),
			.span_i = static_cast<uint32_t>(blur_half_yi),
			.span_f = static_cast<float>(blur_yf * cs_cbuff_blur::quantize_denom),
			.inv_span = static_cast<float>(1 / ((blur_half_yi + blur_yf) * cs_cbuff_blur::quantize_denom)),
		}),
		D3D::create_const_buffer(cs_cbuff_blur{
			.size_src_x = static_cast<uint32_t>(width_src), .size_src_y = static_cast<uint32_t>(height_src + 2 * blur_half_yi),
			.size_dst_x = static_cast<uint32_t>(width_src + blur_half_xi), .size_dst_y = static_cast<uint32_t>(height_src + 2 * blur_half_yi),
			.span_i = static_cast<uint32_t>(blur_half_xi),
			.span_f = static_cast<float>(blur_xf * cs_cbuff_blur::quantize_denom),
			.inv_span = static_cast<float>(1 / ((blur_half_xi + blur_xf) * cs_cbuff_blur::quantize_denom)),
		}),
		D3D::create_const_buffer(cs_cbuff_blur{
			.size_src_x = static_cast<uint32_t>(width_src + blur_half_xi), .size_src_y = static_cast<uint32_t>(height_src + 2 * blur_half_yi),
			.size_dst_x = static_cast<uint32_t>(width_src + 2 * blur_half_xi), .size_dst_y = static_cast<uint32_t>(height_src + 2 * blur_half_yi),
			.span_i = static_cast<uint32_t>(blur_half_xi),
			.span_f = static_cast<float>(blur_xf * cs_cbuff_blur::quantize_denom),
			.inv_span = static_cast<float>(1 / ((blur_half_xi + blur_xf) * cs_cbuff_blur::quantize_denom)),
		}),
	};
	if (cbuff[0] == nullptr || cbuff[1] == nullptr ||
		cbuff[2] == nullptr || cbuff[3] == nullptr) return false;

	// sequencially apply shaders.
	D3D::cxt->CSSetShader(height_src > blur_half_yi ? cs_blur_y1.Get() : cs_blur_y2.Get(), nullptr, 0);
	D3D::cxt->CSSetShaderResources(0, 1, &src.srv);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &tmp.uav, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff[0].GetAddressOf());
	D3D::cxt->Dispatch((width_src + ((1 << 6) - 1)) >> 6, 1, 1);

	constexpr ::ID3D11UnorderedAccessView* uav_null = nullptr;
	D3D::cxt->CSSetShader(cs_blur_y1.Get(), nullptr, 0);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_null, nullptr);
	D3D::cxt->CSSetShaderResources(0, 1, &tmp.srv);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &src.uav, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff[1].GetAddressOf());
	D3D::cxt->Dispatch((width_src + ((1 << 6) - 1)) >> 6, 1, 1);

	D3D::cxt->CSSetShader(width_src > blur_half_xi ? cs_blur_x1.Get() : cs_blur_x2.Get(), nullptr, 0);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_null, nullptr);
	D3D::cxt->CSSetShaderResources(0, 1, &src.srv);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &tmp.uav, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff[2].GetAddressOf());
	D3D::cxt->Dispatch(1, ((height_src + 2 * blur_half_yi) + ((1 << 6) - 1)) >> 6, 1);

	D3D::cxt->CSSetShader(cs_blur_x1.Get(), nullptr, 0);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_null, nullptr);
	D3D::cxt->CSSetShaderResources(0, 1, &tmp.srv);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &src.uav, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff[3].GetAddressOf());
	D3D::cxt->Dispatch(1, ((height_src + 2 * blur_half_yi) + ((1 << 6) - 1)) >> 6, 1);

	// cleanup.
	D3D::cxt->ClearState();

	return true;
}

bool blur_gaussian(int width_src, int height_src,
	D3D::cs_views const& src, D3D::cs_views const& tmp,
	double blur_half_x, double blur_half_y)
{
	int const
		blur_half_xi = static_cast<int>(std::ceil(blur_half_x)),
		blur_half_yi = static_cast<int>(std::ceil(blur_half_y));

	// the triangular distribution of the support [-1, +1] has the variance of 1/6.
	const double
		inv_vari_x = 6 / ((blur_half_x + 0.5) * (blur_half_x + 0.5)),
		inv_vari_y = 6 / ((blur_half_y + 0.5) * (blur_half_y + 0.5));
	double wt_x = 1, wt_y = 1;
	for (int x = 1; x <= blur_half_xi; x++)
		wt_x += 2 * std::exp(-0.5 * x * x * inv_vari_x);
	if (inv_vari_x == inv_vari_y) [[likely]] wt_y = wt_x;
	else {
		for (int y = 1; y <= blur_half_yi; y++)
			wt_y += 2 * std::exp(-0.5 * y * y * inv_vari_y);
	}

	// create constant buffer.
	D3D::ComPtr<::ID3D11Buffer> cbuff[] = {
		D3D::create_const_buffer(cs_cbuff_gauss_blur{
			.size_src_x = static_cast<uint32_t>(width_src), .size_src_y = static_cast<uint32_t>(height_src),
			.size_dst_x = static_cast<uint32_t>(width_src + 2 * blur_half_xi), .size_dst_y = static_cast<uint32_t>(height_src),
			.rate1 = static_cast<float>(std::exp(-0.5 * inv_vari_x)),
			.rate3 = static_cast<float>(std::exp(-1.5 * inv_vari_x)),
			.rate4 = static_cast<float>(std::exp(-2.0 * inv_vari_x)),
			.rate8 = static_cast<float>(std::exp(-4.0 * inv_vari_x)),
			.span_i = static_cast<uint32_t>(blur_half_xi),
			.inv_span = static_cast<float>(1 / wt_x),
		}),
		D3D::create_const_buffer(cs_cbuff_gauss_blur{
			.size_src_x = static_cast<uint32_t>(width_src + 2 * blur_half_xi), .size_src_y = static_cast<uint32_t>(height_src),
			.size_dst_x = static_cast<uint32_t>(width_src + 2 * blur_half_xi), .size_dst_y = static_cast<uint32_t>(height_src + 2 * blur_half_yi),
			.rate1 = static_cast<float>(std::exp(-0.5 * inv_vari_y)),
			.rate3 = static_cast<float>(std::exp(-1.5 * inv_vari_y)),
			.rate4 = static_cast<float>(std::exp(-2.0 * inv_vari_y)),
			.rate8 = static_cast<float>(std::exp(-4.0 * inv_vari_y)),
			.span_i = static_cast<uint32_t>(blur_half_yi),
			.inv_span = static_cast<float>(1 / wt_y),
		}),
	};
	if (cbuff[0] == nullptr || cbuff[1] == nullptr) return false;

	// create sampler state.
	auto smp = D3D::create_sampler_state(D3D11_FILTER_MIN_MAG_MIP_LINEAR);

	// sequencially apply shaders.
	D3D::cxt->CSSetShader(cs_gauss_blur_x.Get(), nullptr, 0);
	D3D::cxt->CSSetShaderResources(0, 1, &src.srv);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &tmp.uav, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff[0].GetAddressOf());
	D3D::cxt->CSSetSamplers(0, 1, smp.GetAddressOf());
	D3D::cxt->Dispatch(
		((width_src + 2 * blur_half_xi) + ((1 << 3) - 1)) >> 3,
		(height_src + ((1 << 3) - 1)) >> 3, 1);

	constexpr ::ID3D11UnorderedAccessView* uav_null = nullptr;
	D3D::cxt->CSSetShader(cs_gauss_blur_y.Get(), nullptr, 0);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_null, nullptr);
	D3D::cxt->CSSetShaderResources(0, 1, &tmp.srv);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &src.uav, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff[1].GetAddressOf());
	D3D::cxt->Dispatch(
		((width_src + 2 * blur_half_xi) + ((1 << 3) - 1)) >> 3,
		((height_src + 2 * blur_half_yi) + ((1 << 3) - 1)) >> 3, 1);

	// cleanup.
	D3D::cxt->ClearState();

	return true;
}
ANON_NS_E

bool ops::blur(blur_type type,
	int width_src, int height_src,
	D3D::cs_views const& src, D3D::cs_views const& tmp,
	double blur_half_x, double blur_half_y)
{
	if (blur_half_x <= 0 && blur_half_y <= 0) return true; // nothing to do.

	if (!init()) return false;

	switch (type) {
	case blur_type::triangular: default:
		return blur_triangular(width_src, height_src, src, tmp, blur_half_x, blur_half_y);
	case blur_type::gaussian:
		return blur_gaussian(width_src, height_src, src, tmp, blur_half_x, blur_half_y);
	}
}

bool ops::delta_move(
	int width, int height,
	::ID3D11ShaderResourceView* srv_src, ::ID3D11UnorderedAccessView* uav_dst,
	double delta_x, double delta_y)
{
	if (!init()) return false;

	// create constant buffer.
	auto cbuff = D3D::create_const_buffer(cs_cbuff_delta_move{
		.size_dst_x = static_cast<uint32_t>(width), .size_dst_y = static_cast<uint32_t>(height),
		.delta_x = static_cast<float>(delta_x),
		.delta_y = static_cast<float>(delta_y),
	});
	if (cbuff == nullptr) return false;

	// create sampler state.
	auto smp = D3D::create_sampler_state(D3D11_FILTER_MIN_MAG_MIP_LINEAR);

	// execute shader.
	D3D::cxt->CSSetShader(cs_delta_move.Get(), nullptr, 0);
	D3D::cxt->CSSetShaderResources(0, 1, &srv_src);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_dst, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff.GetAddressOf());
	D3D::cxt->CSSetSamplers(0, 1, smp.GetAddressOf());
	D3D::cxt->Dispatch(
		(width + ((1 << 3) - 1)) >> 3,
		(height + ((1 << 3) - 1)) >> 3, 1);

	// cleanup.
	D3D::cxt->ClearState();
	return true;
}
