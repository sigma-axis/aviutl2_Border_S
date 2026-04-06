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
using color_float = Border_S::image_ops::color_float;
using ops = Border_S::image_ops::ops;

#define ANON_NS_B namespace {
#define ANON_NS_E }


ANON_NS_B;
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

constexpr char cs_src_blur_x[] = R"(
Texture2D<float> src : register(t0);
RWTexture2D<float> dst : register(u0);
cbuffer constant0 : register(b0) {
	uint2 size_src;
	uint2 size_dst;
	uint span_i;
	float span_f, inv_span;
};
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_dst)) return;

	float sum = 0;
	for (uint x = 0; x < span_i; x++) {
		const uint src_x = id.x - x;
		if (src_x < size_src.x)
			sum += src[uint2(src_x, id.y)];
	}
	{
		const uint src_x = id.x - span_i;
		if (src_x < size_src.x)
			sum += span_f * src[uint2(src_x, id.y)];
	}

	dst[id] = inv_span * sum;
}
)";
constexpr char cs_src_blur_y[] = R"(
RWTexture2D<float> dst : register(u0);
Texture2D<float> src : register(t0);
cbuffer constant0 : register(b0) {
	uint2 size_src;
	uint2 size_dst;
	uint span_i;
	float span_f, inv_span;
};
[numthreads(8, 8, 1)]
void csmain(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= size_dst)) return;

	float sum = 0;
	for (uint y = 0; y < span_i; y++) {
		const uint src_y = id.y - y;
		if (src_y < size_src.y)
			sum += src[uint2(id.x, src_y)];
	}
	{
		const uint src_y = id.y - span_i;
		if (src_y < size_src.y)
			sum += span_f * src[uint2(id.x, src_y)];
	}

	dst[size_dst - 1 - id] = inv_span * sum;
}
)";
struct cs_cbuff_blur {
	uint32_t size_src_x, size_src_y;
	uint32_t size_dst_x, size_dst_y;
	uint32_t span_i;
	float span_f, inv_span;

	[[maybe_unused]] uint8_t _pad[4];
};
static_assert(sizeof(cs_cbuff_blur) % 16 == 0);

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

	const float2 pos_src = clamp(float2(id) + 0.5 - delta, 0, size_dst);
	dst[id] = src.SampleLevel(smp, pos_src * inv_size_src, 0);
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
	cs_draw, cs_recolor, cs_recolor_empty,
	cs_carve, cs_carve_1, cs_combine,
	cs_blur_x, cs_blur_y, cs_delta_move;
void quit()
{
	cs_extract_alpha.Reset();
	cs_draw.Reset();
	cs_recolor.Reset();
	cs_recolor_empty.Reset();
	cs_carve.Reset();
	cs_carve_1.Reset();
	cs_combine.Reset();
	cs_blur_x.Reset();
	cs_blur_y.Reset();
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
			(cs_recolor = D3D::create_compute_shader(cs_src(recolor))) != nullptr &&
			(cs_recolor_empty = D3D::create_compute_shader(cs_src(recolor_empty))) != nullptr &&
			(cs_carve = D3D::create_compute_shader(cs_src(carve))) != nullptr &&
			(cs_carve_1 = D3D::create_compute_shader(cs_src(carve_1))) != nullptr &&
			(cs_combine = D3D::create_compute_shader(cs_src(combine))) != nullptr &&
			(cs_blur_x = D3D::create_compute_shader(cs_src(blur_x))) != nullptr &&
			(cs_blur_y = D3D::create_compute_shader(cs_src(blur_y))) != nullptr &&
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
	color_float const& color,
	double alpha_front, double alpha_back)
{
	if (!init()) return false;

	// create constant buffer.
	auto cbuff = D3D::create_const_buffer(cs_cbuff_draw{
		.size_src_x = static_cast<uint32_t>(width_src), .size_src_y = static_cast<uint32_t>(height_src),
		.size_dst_x = static_cast<uint32_t>(width_dst), .size_dst_y = static_cast<uint32_t>(height_dst),
		.color = color,
		.offset_x = offset_x, .offset_y = offset_y,
		.a_front = static_cast<float>(alpha_front),
		.a_back = static_cast<float>(alpha_back),
	});
	if (cbuff == nullptr) return false;

	// execute shader.
	D3D::cxt->CSSetShader(cs_draw.Get(), nullptr, 0);
	::ID3D11ShaderResourceView* const srv_draw[] = { srv_src, srv_shape };
	D3D::cxt->CSSetShaderResources(0, std::size(srv_draw), srv_draw);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_dst, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff.GetAddressOf());
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
	color_float const& color, bool invert,
	double alpha_front, double alpha_back)
{
	if (!init()) return false;

	if (width_shape > 0 && height_shape > 0 && srv_shape != nullptr) {
		// create constant buffer.
		auto cbuff = D3D::create_const_buffer(cs_cbuff_recolor{
			.size_shape_x = static_cast<uint32_t>(width_shape), .size_shape_y = static_cast<uint32_t>(height_shape),
			.size_dst_x = static_cast<uint32_t>(width_dst), .size_dst_y = static_cast<uint32_t>(height_dst),
			.color = color,
			.offset_x = offset_x, .offset_y = offset_y,
			.a_front = static_cast<float>(alpha_front),
			.a_back = static_cast<float>(alpha_back),
			.alpha_base = invert ? 1.0f : 0.0f,
		});
		if (cbuff == nullptr) return false;

		// execute shader.
		D3D::cxt->CSSetShader(cs_recolor.Get(), nullptr, 0);
		D3D::cxt->CSSetShaderResources(0, 1, &srv_shape);
		D3D::cxt->CSSetConstantBuffers(0, 1, cbuff.GetAddressOf());
	}
	else {
		// shape is recognized as empty.

		// create constant buffer.
		auto cbuff = D3D::create_const_buffer(cs_cbuff_recolor_emtpy{
			.color = color,
			.size_dst_x = static_cast<uint32_t>(width_dst), .size_dst_y = static_cast<uint32_t>(height_dst),
			.a_front = invert ? static_cast<float>(alpha_front) : 0,
			.a_back = static_cast<float>(alpha_back),
		});
		if (cbuff == nullptr) return false;

		// execute shader.
		D3D::cxt->CSSetShader(cs_recolor_empty.Get(), nullptr, 0);
		D3D::cxt->CSSetConstantBuffers(0, 1, cbuff.GetAddressOf());
	}

	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_dst, nullptr);
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
	color_float const& color,
	double alpha_src, double alpha_shape, bool is_src_front)
{
	if (!init()) return false;

	// create constant buffer.
	auto cbuff = D3D::create_const_buffer(cs_cbuff_combine{
		.size_src_x = static_cast<uint32_t>(width_src), .size_src_y = static_cast<uint32_t>(height_src),
		.size_shape_x = static_cast<uint32_t>(width_shape), .size_shape_y = static_cast<uint32_t>(height_shape),
		.size_dst_x = static_cast<uint32_t>(width_dst), .size_dst_y = static_cast<uint32_t>(height_dst),
		.is_src_front = is_src_front,
		.color = color,
		.offset_src_x = offset_src_x, .offset_src_y = offset_src_y,
		.offset_shape_x = offset_shape_x, .offset_shape_y = offset_shape_y,
		.a_src = static_cast<float>(alpha_src),
		.a_shape = static_cast<float>(alpha_shape),
	});
	if (cbuff == nullptr) return false;

	// execute shader.
	D3D::cxt->CSSetShader(cs_combine.Get(), nullptr, 0);
	::ID3D11ShaderResourceView* const srv_combine[] = { srv_src, srv_shape };
	D3D::cxt->CSSetShaderResources(0, std::size(srv_combine), srv_combine);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_dst, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff.GetAddressOf());
	D3D::cxt->Dispatch(
		(width_dst + ((1 << 3) - 1)) >> 3,
		(height_dst + ((1 << 3) - 1)) >> 3, 1);

	// cleanup.
	D3D::cxt->ClearState();

	return true;
}

bool ops::blur(
	int width_src, int height_src,
	D3D::cs_views const& src, D3D::cs_views const& tmp,
	double blur_half_x, double blur_half_y)
{
	if (blur_half_x <= 0 && blur_half_y <= 0) return true; // nothing to do.

	if (!init()) return false;

	int const
		blur_half_xi = static_cast<int>(std::ceil(blur_half_x)),
		blur_half_yi = static_cast<int>(std::ceil(blur_half_y));

	// create constant buffer.
	D3D::ComPtr<::ID3D11Buffer> cbuff[] = {
		D3D::create_const_buffer(cs_cbuff_blur{
			.size_src_x = static_cast<uint32_t>(width_src), .size_src_y = static_cast<uint32_t>(height_src),
			.size_dst_x = static_cast<uint32_t>(width_src + blur_half_xi), .size_dst_y = static_cast<uint32_t>(height_src),
			.span_i = static_cast<uint32_t>(blur_half_xi),
			.span_f = static_cast<float>(blur_half_x - (blur_half_xi - 1)),
			.inv_span = static_cast<float>(1 / (blur_half_x + 1)),
		}),
		D3D::create_const_buffer(cs_cbuff_blur{
			.size_src_x = static_cast<uint32_t>(width_src + blur_half_xi), .size_src_y = static_cast<uint32_t>(height_src),
			.size_dst_x = static_cast<uint32_t>(width_src + blur_half_xi), .size_dst_y = static_cast<uint32_t>(height_src + blur_half_yi),
			.span_i = static_cast<uint32_t>(blur_half_yi),
			.span_f = static_cast<float>(blur_half_y - (blur_half_yi - 1)),
			.inv_span = static_cast<float>(1 / (blur_half_y + 1)),
		}),
		D3D::create_const_buffer(cs_cbuff_blur{
			.size_src_x = static_cast<uint32_t>(width_src + blur_half_xi), .size_src_y = static_cast<uint32_t>(height_src + blur_half_yi),
			.size_dst_x = static_cast<uint32_t>(width_src + 2 * blur_half_xi), .size_dst_y = static_cast<uint32_t>(height_src + blur_half_yi),
			.span_i = static_cast<uint32_t>(blur_half_xi),
			.span_f = static_cast<float>(blur_half_x - (blur_half_xi - 1)),
			.inv_span = static_cast<float>(1 / (blur_half_x + 1)),
		}),
		D3D::create_const_buffer(cs_cbuff_blur{
			.size_src_x = static_cast<uint32_t>(width_src + 2 * blur_half_xi), .size_src_y = static_cast<uint32_t>(height_src + blur_half_yi),
			.size_dst_x = static_cast<uint32_t>(width_src + 2 * blur_half_xi), .size_dst_y = static_cast<uint32_t>(height_src + 2 * blur_half_yi),
			.span_i = static_cast<uint32_t>(blur_half_yi),
			.span_f = static_cast<float>(blur_half_y - (blur_half_yi - 1)),
			.inv_span = static_cast<float>(1 / (blur_half_y + 1)),
		}),
	};
	if (cbuff[0] == nullptr || cbuff[1] == nullptr || 
		cbuff[2] == nullptr || cbuff[3] == nullptr) return false;

	// sequencially apply shaders.
	D3D::cxt->CSSetShader(cs_blur_x.Get(), nullptr, 0);
	D3D::cxt->CSSetShaderResources(0, 1, &src.srv);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &tmp.uav, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff[0].GetAddressOf());
	D3D::cxt->Dispatch(
		((width_src + blur_half_xi) + ((1 << 3) - 1)) >> 3,
		(height_src + ((1 << 3) - 1)) >> 3, 1);

	constexpr ::ID3D11UnorderedAccessView* uav_null = nullptr;
	D3D::cxt->CSSetShader(cs_blur_y.Get(), nullptr, 0);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_null, nullptr);
	D3D::cxt->CSSetShaderResources(0, 1, &tmp.srv);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &src.uav, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff[1].GetAddressOf());
	D3D::cxt->Dispatch(
		((width_src + blur_half_xi) + ((1 << 3) - 1)) >> 3,
		((height_src + blur_half_yi) + ((1 << 3) - 1)) >> 3, 1);

	D3D::cxt->CSSetShader(cs_blur_x.Get(), nullptr, 0);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_null, nullptr);
	D3D::cxt->CSSetShaderResources(0, 1, &src.srv);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &tmp.uav, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff[2].GetAddressOf());
	D3D::cxt->Dispatch(
		((width_src + 2 * blur_half_xi) + ((1 << 3) - 1)) >> 3,
		((height_src + blur_half_yi) + ((1 << 3) - 1)) >> 3, 1);

	D3D::cxt->CSSetShader(cs_blur_y.Get(), nullptr, 0);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_null, nullptr);
	D3D::cxt->CSSetShaderResources(0, 1, &tmp.srv);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &src.uav, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff[3].GetAddressOf());
	D3D::cxt->Dispatch(
		((width_src + 2 * blur_half_xi) + ((1 << 3) - 1)) >> 3,
		((height_src + 2 * blur_half_yi) + ((1 << 3) - 1)) >> 3, 1);

	// cleanup.
	D3D::cxt->ClearState();

	return true;
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
	auto smp = D3D::create_sampler_state(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);

	// execute shader.
	D3D::cxt->CSSetShader(cs_delta_move.Get(), nullptr, 0);
	D3D::cxt->CSSetShaderResources(0, 1, &srv_src);
	D3D::cxt->CSSetUnorderedAccessViews(0, 1, &uav_dst, nullptr);
	D3D::cxt->CSSetConstantBuffers(0, 1, cbuff.GetAddressOf());
	D3D::cxt->CSSetSamplers(0, 1, &smp);
	D3D::cxt->Dispatch(
		(width + ((1 << 3) - 1)) >> 3,
		(height + ((1 << 3) - 1)) >> 3, 1);

	// cleanup.
	D3D::cxt->ClearState();
	return true;
}
