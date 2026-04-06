/*
The MIT License (MIT)

Copyright (c) 2026 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <cstdint>
#include <cmath>
#include <algorithm>

#include "d3d_service.hpp"
using D3D = d3d_service::D3D;

#include "method_bin.hpp"
#include "method_bin2x.hpp"
#include "method_bin_smooth.hpp"
#include "method_sum.hpp"
#include "method_max.hpp"
using bin = Border_S::method::bin;
using bin2x = Border_S::method::bin2x;
using bin_sm = Border_S::method::bin_smooth;
using sum = Border_S::method::sum;
using max = Border_S::method::max;

#include "image_ops.hpp"
using color_float = Border_S::image_ops::color_float;
using ops = Border_S::image_ops::ops;

#include "Filters_Common.hpp"
namespace common = Border_S::Filter::common;

#define ANON_NS_B namespace {
#define ANON_NS_E }


////////////////////////////////
// helper functions.
////////////////////////////////
ANON_NS_B
// returns the integer and fractional parts of x, where the integer part is rounded to the nearest integer (half rounds up).
// usage: auto [i, f] = int_frac(x);
inline auto int_frac(double x)
{
	struct ret { int i; double f; };
	int const i = static_cast<int>(std::floor(x + 0.5));
	return ret{ i, x - i };
}

// avoids exactly half-integral values.
inline void avoid_half_integral(double& x)
{
	if (x != 0 && (static_cast<int>(std::round(2000 * x)) - 1000) % 2000 == 0)
		x += 1.0 / 2000;
}
// avoids exactly quarter-integral values.
inline void avoid_quarter_integral(double& x)
{
	if (x != 0 && (static_cast<int>(std::round(4000 * x)) - 1000) % 2000 == 0)
		x += 1.0 / 4000;
}
ANON_NS_E


////////////////////////////////
// exports.
////////////////////////////////
bool common::push_alpha(double alpha, FILTER_PROC_VIDEO* video)
{
	auto const obj = video->get_image_texture2d();
	if (!D3D::init(obj)) return false;

	auto uav_src = D3D::to_unordered_access_view(obj);
	if (uav_src == nullptr) return false;

	return ops::push_alpha(
		video->object->width, video->object->height,
		uav_src.Get(), alpha);
}

bool common::add_size(int diff_size_x, int diff_size_y, FILTER_PROC_VIDEO* video)
{
	auto obj = video->get_image_texture2d();
	if (!D3D::init(obj)) return false;

	auto src_obj = D3D::clone(obj);
	if (src_obj == nullptr) return false;

	int const width_src = video->object->width, height_src = video->object->height;
	int const
		diff_x = std::min(std::max(diff_size_x, -((width_src - 1) >> 1)),
			(static_cast<int>(D3D::max_image_size) - width_src) >> 1),
		diff_y = std::min(std::max(diff_size_y, -((height_src - 1) >> 1)),
			(static_cast<int>(D3D::max_image_size) - height_src) >> 1);
	int const width_dst = width_src + 2 * diff_x, height_dst = height_src + 2 * diff_y;

	video->set_image_data(nullptr, width_dst, height_dst);
	obj = video->get_image_texture2d();
	if (obj == nullptr) return false;

	auto rtv_obj = D3D::to_render_target_view(obj);
	if (rtv_obj == nullptr) return false;
	D3D::cxt->ClearRenderTargetView(rtv_obj.Get(), D3D::zero_color);
	::D3D11_BOX const box{
		.left = static_cast<uint32_t>(std::max(-diff_x, 0)),
		.top = static_cast<uint32_t>(std::max(-diff_y, 0)),
		.front = 0,
		.right = static_cast<uint32_t>(std::min(-diff_x + width_dst, width_src)),
		.bottom = static_cast<uint32_t>(std::min(-diff_y + height_dst, height_src)),
		.back = 1,
	};
	D3D::cxt->CopySubresourceRegion(obj, 0,
		static_cast<uint32_t>(std::max(diff_x, 0)), static_cast<uint32_t>(std::max(diff_y, 0)), 0,
		src_obj.Get(), 0, &box);

	return true;
}

D3D::ComPtr<::ID3D11ShaderResourceView> common::sequential_inf_def(
	int width_src, int height_src,
	int width_dst, int height_dst,
	double offset_x, double offset_y, // offset of source within dest.
	::ID3D11ShaderResourceView* srv_src, bool is_src_scalar,
	double const* inf_def_seq, int inf_def_num, double blur,
	double aspect_x, double aspect_y,
	double superellipse_exp,
	methods::id method, double a_param)
{
	auto [offset_xi, delta_x] = int_frac(offset_x);
	auto [offset_yi, delta_y] = int_frac(offset_y);

	// avoid exactly half-integral or quarter-integral offsets.
	switch (method) {
	case methods::bin: case methods::bin_smooth: case methods::max:
	{
		avoid_half_integral(delta_x);
		avoid_half_integral(delta_y);
		break;
	}
	case methods::bin2x:
	{
		avoid_quarter_integral(delta_x);
		avoid_quarter_integral(delta_y);
		break;
	}
	default: break;
	}

	// determine the maximum canvas size.
	int const
		blur_xi = static_cast<int>(std::ceil(aspect_x * blur)),
		blur_yi = static_cast<int>(std::ceil(aspect_y * blur));
	int width_max = std::max(width_src, width_dst),
		height_max = std::max(height_src, height_dst);
	for (int i = 0, infl_x = 0, infl_y = 0; i < inf_def_num - 1; i++) {
		auto const& r = inf_def_seq[i];
		auto const rx = aspect_x * r, ry = aspect_y * r;
		infl_x += static_cast<int>(std::ceil(rx));
		infl_y += static_cast<int>(std::ceil(ry));
		D3D::clamp_extension_2d(infl_x, width_src);
		D3D::clamp_extension_2d(infl_y, height_src);
		width_max = std::max(width_max, width_src + 2 * infl_x);
		height_max = std::max(height_max, height_src + 2 * infl_y);
	}

	// create the buffer for the shape.
	D3D::ComPtr<::ID3D11Texture2D> shapes[] = {
		D3D::create_texture(::DXGI_FORMAT_R32_FLOAT, width_max, height_max),
		D3D::create_texture(::DXGI_FORMAT_R32_FLOAT, width_max, height_max),
	};
	if (shapes[0] == nullptr || shapes[1] == nullptr) return nullptr;

	// prepare views.
	D3D::ComPtr<::ID3D11UnorderedAccessView> uav_shapes[] = {
		D3D::to_unordered_access_view(shapes[0].Get()),
		D3D::to_unordered_access_view(shapes[1].Get()),
	};
	if (uav_shapes[0] == nullptr || uav_shapes[1] == nullptr) return nullptr;
	D3D::ComPtr<::ID3D11ShaderResourceView> srv_shapes[] = {
		D3D::to_shader_resource_view(shapes[0].Get()),
		D3D::to_shader_resource_view(shapes[1].Get()),
	};
	if (srv_shapes[0] == nullptr || srv_shapes[1] == nullptr) return nullptr;

	// copy alpha channel.
	if (int const
		ofs_x = inf_def_num > 0 ? 0 : offset_xi - blur_xi,
		ofs_y = inf_def_num > 0 ? 0 : offset_yi - blur_yi;
		is_src_scalar) {
		D3D::ComPtr<::ID3D11Resource> src;
		srv_src->GetResource(&src);
		if (src == nullptr) return nullptr;
		::D3D11_BOX const box = {
			.left = static_cast<uint32_t>(std::max(-ofs_x, 0)),
			.top = static_cast<uint32_t>(std::max(-ofs_y, 0)),
			.front = 0,
			.right = static_cast<uint32_t>(std::min(width_src, width_dst - ofs_x)),
			.bottom = static_cast<uint32_t>(std::min(height_src, height_dst - ofs_y)),
			.back = 1,
		};
		D3D::cxt->CopySubresourceRegion(shapes[0].Get(), 0,
			static_cast<uint32_t>(std::max(ofs_x, 0)), static_cast<uint32_t>(std::max(ofs_y, 0)),
			0, src.Get(), 0, &box);
	}
	else {
		if (inf_def_num <= 0)
			D3D::cxt->ClearUnorderedAccessViewFloat(uav_shapes[0].Get(), D3D::zero_color);
		if (!ops::extract_alpha(width_src, height_src,
			0, 0, srv_src, ofs_x, ofs_y, uav_shapes[0].Get())) return nullptr;
	}
	size_t idx_curr_shape_src = 0;

	if (inf_def_num > 0) {
		switch (method) {
		case methods::bin:
		case methods::bin2x:
		case methods::bin_smooth:
		{
			// uses method_bin even for method_bin2x except the last one of the sequence.
			static_assert(
				bin::buff_spec::elem_size_arc == bin2x::buff_spec::elem_size_arc &&
				bin::buff_spec::elem_size_mid == bin2x::buff_spec::elem_size_mid &&
				bin::buff_spec::elem_size_mid == bin_sm::buff_spec::elem_size_mid);

			// prepare intermediate buffers.
			uint32_t max_arc_length = 1, max_disk_width = 1, max_disk_height = 1, max_mid_length = 1;
			for (int i = 0, ofs1_x = 0, ofs1_y = 0; i < inf_def_num; i++) {
				auto const& r = inf_def_seq[i];
				auto const R = std::abs(r), Rx = aspect_x * R, Ry = aspect_y * R;
				int ofs2_x = offset_xi - blur_xi, ofs2_y = offset_yi - blur_yi,
					w2 = width_dst, h2 = height_dst;
				if (i < inf_def_num - 1) {
					ofs2_x = static_cast<int>(std::ceil(aspect_x * r)) + ofs1_x;
					ofs2_y = static_cast<int>(std::ceil(aspect_y * r)) + ofs1_y;
					D3D::clamp_extension_2d(ofs2_x, width_src);
					D3D::clamp_extension_2d(ofs2_y, height_src);
					w2 = width_src + 2 * ofs2_x;
					h2 = height_src + 2 * ofs2_y;
				}

				uint32_t arc_length = 0, disk_width = 0, disk_height = 0, mid_length;
				if (i < inf_def_num - 1 || method == methods::bin) {
					bin::buff_spec::get_size_arc(Rx, Ry, arc_length);
					bin::buff_spec::get_size_mid(
						width_src + 2 * ofs1_x, height_src + 2 * ofs1_y, w2, h2, mid_length);
				}
				else if (method == methods::bin2x) {
					bin2x::buff_spec::get_size_arc(Rx, Ry, arc_length);
					bin2x::buff_spec::get_size_mid(
						width_src + 2 * ofs1_x, height_src + 2 * ofs1_y, w2, h2, mid_length);
				}
				else {
					bin_sm::buff_spec::get_size_disk(Rx, Ry, disk_width, disk_height);
					bin_sm::buff_spec::get_size_mid(
						width_src + 2 * ofs1_x, height_src + 2 * ofs1_y, w2, h2, mid_length);
				}
				ofs1_x = ofs2_x; ofs1_y = ofs2_y;

				max_arc_length = std::max(max_arc_length, arc_length);
				max_disk_width = std::max(max_disk_width, disk_width);
				max_disk_height = std::max(max_disk_height, disk_height);
				max_mid_length = std::max(max_mid_length, mid_length);
			}
			D3D::ComPtr<::ID3D11Buffer> arc = nullptr, mid = nullptr;
			D3D::ComPtr<::ID3D11Texture2D> disk = nullptr;
			if (inf_def_num > 1 || method != methods::bin_smooth) {
				arc = D3D::create_structured_buffer(bin::buff_spec::elem_size_arc, max_arc_length);
				if (arc == nullptr) return nullptr;
			}
			if (method == methods::bin_smooth) {
				disk = D3D::create_texture(bin_sm::buff_spec::format_disk, max_disk_width, max_disk_height);
				if (disk == nullptr) return nullptr;
			}
			mid = D3D::create_structured_buffer(bin::buff_spec::elem_size_mid, max_mid_length);
			if (mid == nullptr) return nullptr;
			D3D::ComPtr<::ID3D11UnorderedAccessView> uav_arc = nullptr, uav_disk = nullptr, uav_mid = nullptr;
			D3D::ComPtr<::ID3D11ShaderResourceView> srv_arc = nullptr, srv_disk = nullptr, srv_mid = nullptr;
			if (arc != nullptr) {
				uav_arc = D3D::to_unordered_access_view(arc.Get());
				if (uav_arc == nullptr) return nullptr;
				srv_arc = D3D::to_shader_resource_view(arc.Get());
				if (srv_arc == nullptr) return nullptr;
			}
			if (disk != nullptr) {
				uav_disk = D3D::to_unordered_access_view(disk.Get());
				if (uav_disk == nullptr) return nullptr;
				srv_disk = D3D::to_shader_resource_view(disk.Get());
				if (srv_disk == nullptr) return nullptr;
			}
			uav_mid = D3D::to_unordered_access_view(mid.Get());
			if (uav_mid == nullptr) return nullptr;
			srv_mid = D3D::to_shader_resource_view(mid.Get());
			if (srv_mid == nullptr) return nullptr;

			// apply inflation and deflation sequentially.
			int ofs1_x = 0, ofs1_y = 0;
			double thresh = std::min<double>(a_param, D3D::max_f16_lt_1);
			for (int i = 0; i < inf_def_num; i++) {
				auto const& r = inf_def_seq[i];
				auto const R = std::abs(r), Rx = aspect_x * R, Ry = aspect_y * R,
					rx = aspect_x * r, ry = aspect_y * r;
				int ofs2_x = offset_xi - blur_xi,
					ofs2_y = offset_yi - blur_yi,
					w2 = width_dst, h2 = height_dst;
				double dx = delta_x, dy = delta_y;
				if (i < inf_def_num - 1) {
					ofs2_x = static_cast<int>(std::ceil(rx)) + ofs1_x;
					ofs2_y = static_cast<int>(std::ceil(ry)) + ofs1_y;
					D3D::clamp_extension_2d(ofs2_x, width_src);
					D3D::clamp_extension_2d(ofs2_y, height_src);
					w2 = width_src + 2 * ofs2_x;
					h2 = height_src + 2 * ofs2_y;
					dx = 0; dy = 0;
				}

				if (i < inf_def_num - 1 || method == methods::bin) {
					// method::bin
					if (!bin::inflate(r < 0,
						width_src + 2 * ofs1_x, height_src + 2 * ofs1_y, w2, h2,
						ofs2_x - ofs1_x, ofs2_y - ofs1_y, dx, dy,
						srv_shapes[idx_curr_shape_src].Get(), uav_shapes[idx_curr_shape_src ^ 1].Get(),
						thresh, Rx, Ry, superellipse_exp,
						{ srv_arc.Get(), uav_arc.Get() }, { srv_mid.Get(), uav_mid.Get() })) return nullptr;
				}
				else if (method == methods::bin2x) {
					// method::bin2x
					if (!bin2x::inflate(r < 0,
						width_src + 2 * ofs1_x, height_src + 2 * ofs1_y, w2, h2,
						ofs2_x - ofs1_x, ofs2_y - ofs1_y, dx, dy,
						srv_shapes[idx_curr_shape_src].Get(), uav_shapes[idx_curr_shape_src ^ 1].Get(),
						thresh, Rx, Ry, superellipse_exp,
						{ srv_arc.Get(), uav_arc.Get() }, { srv_mid.Get(), uav_mid.Get() })) return nullptr;
				}
				else {
					// method::bin_smooth
					if (!bin_sm::inflate(r < 0,
						width_src + 2 * ofs1_x, height_src + 2 * ofs1_y, w2, h2,
						ofs2_x - ofs1_x, ofs2_y - ofs1_y, dx, dy,
						srv_shapes[idx_curr_shape_src].Get(), uav_shapes[idx_curr_shape_src ^ 1].Get(),
						thresh, Rx, Ry, superellipse_exp,
						{ srv_disk.Get(), uav_disk.Get() }, { srv_mid.Get(), uav_mid.Get() })) return nullptr;
				}

				ofs1_x = ofs2_x; ofs1_y = ofs2_y;
				idx_curr_shape_src ^= 1;

				thresh = methods::second_param_a(thresh, methods::bin); // alpha values are binarized at this phase.
			}
			break;
		}
		case methods::sum:
		{
			// prepare intermediate buffers.
			uint32_t max_disk_width = 1, max_disk_height = 1, max_arc_length = 1, max_mid_length = 1;
			for (int i = 0, ofs1_x = 0, ofs1_y = 0; i < inf_def_num; i++) {
				auto const& r = inf_def_seq[i];
				auto const R = std::abs(r), Rx = aspect_x * R, Ry = aspect_y * R;
				int ofs2_x = offset_xi - blur_xi, ofs2_y = offset_yi - blur_yi,
					w2 = width_dst, h2 = height_dst;
				if (i < inf_def_num - 1) {
					ofs2_x = static_cast<int>(std::ceil(aspect_x * r)) + ofs1_x;
					ofs2_y = static_cast<int>(std::ceil(aspect_y * r)) + ofs1_y;
					D3D::clamp_extension_2d(ofs2_x, width_src);
					D3D::clamp_extension_2d(ofs2_y, height_src);
					w2 = width_src + 2 * ofs2_x;
					h2 = height_src + 2 * ofs2_y;
				}

				uint32_t disk_width, disk_height, arc_length, mid_length;
				sum::buff_spec::get_size_disk(Rx, Ry, disk_width, disk_height);
				sum::buff_spec::get_size_arc(Rx, Ry, arc_length);
				sum::buff_spec::get_size_mid(Rx, Ry,
					w2, h2, ofs2_x - ofs1_x, ofs2_y - ofs1_y, mid_length);
				ofs1_x = ofs2_x; ofs1_y = ofs2_y;

				max_disk_width = std::max(max_disk_width, disk_width);
				max_disk_height = std::max(max_disk_height, disk_height);
				max_arc_length = std::max(max_arc_length, arc_length);
				max_mid_length = std::max(max_mid_length, mid_length);
			}
			auto disk = D3D::create_texture(sum::buff_spec::format_disk, max_disk_width, max_disk_height);
			if (disk == nullptr) return nullptr;
			auto arc = D3D::create_structured_buffer(sum::buff_spec::elem_size_arc, max_arc_length);
			if (arc == nullptr) return nullptr;
			auto mid = D3D::create_structured_buffer(sum::buff_spec::elem_size_mid, max_mid_length);
			if (mid == nullptr) return nullptr;
			auto uav_disk = D3D::to_unordered_access_view(disk.Get());
			if (uav_disk == nullptr) return nullptr;
			auto srv_disk = D3D::to_shader_resource_view(disk.Get());
			if (srv_disk == nullptr) return nullptr;
			auto uav_arc = D3D::to_unordered_access_view(arc.Get());
			if (uav_arc == nullptr) return nullptr;
			auto srv_arc = D3D::to_shader_resource_view(arc.Get());
			if (srv_arc == nullptr) return nullptr;
			auto uav_mid = D3D::to_unordered_access_view(mid.Get());
			if (uav_mid == nullptr) return nullptr;
			auto srv_mid = D3D::to_shader_resource_view(mid.Get());
			if (srv_mid == nullptr) return nullptr;

			// apply inflation and deflation sequentially.
			for (int i = 0, ofs1_x = 0, ofs1_y = 0; i < inf_def_num; i++) {
				auto const& r = inf_def_seq[i];
				auto const R = std::abs(r), Rx = aspect_x * R, Ry = aspect_y * R,
					rx = aspect_x * r, ry = aspect_y * r;
				int ofs2_x = offset_xi - blur_xi,
					ofs2_y = offset_yi - blur_yi,
					w2 = width_dst, h2 = height_dst;
				double dx = delta_x, dy = delta_y;
				if (i < inf_def_num - 1) {
					ofs2_x = static_cast<int>(std::ceil(rx)) + ofs1_x;
					ofs2_y = static_cast<int>(std::ceil(ry)) + ofs1_y;
					D3D::clamp_extension_2d(ofs2_x, width_src);
					D3D::clamp_extension_2d(ofs2_y, height_src);
					w2 = width_src + 2 * ofs2_x;
					h2 = height_src + 2 * ofs2_y;
					dx = 0; dy = 0;
				}

				if (!sum::inflate(r < 0,
					width_src + 2 * ofs1_x, height_src + 2 * ofs1_y, w2, h2,
					ofs2_x - ofs1_x, ofs2_y - ofs1_y,
					srv_shapes[idx_curr_shape_src].Get(), uav_shapes[idx_curr_shape_src ^ 1].Get(),
					a_param, Rx, Ry, superellipse_exp,
					{ srv_disk.Get(), uav_disk.Get() }, { srv_arc.Get(), uav_arc.Get() }, { srv_mid.Get(), uav_mid.Get() })) return nullptr;

				ofs1_x = ofs2_x; ofs1_y = ofs2_y;
				idx_curr_shape_src ^= 1;
			}

			// handle delta moves.
			if (delta_x != 0 || delta_y != 0) {
				ops::delta_move(width_dst, height_dst,
					srv_shapes[idx_curr_shape_src].Get(), uav_shapes[idx_curr_shape_src ^ 1].Get(),
					delta_x, delta_y);
				idx_curr_shape_src ^= 1;
			}
			break;
		}
		case methods::max:
		{
			// prepare intermediate buffers.
			uint32_t max_arc_length = 1, max_mid_width = 1, max_mid_height = 1;
			for (int i = 0, ofs1_x = 0, ofs1_y = 0; i < inf_def_num; i++) {
				auto const& r = inf_def_seq[i];
				auto const R = std::abs(r), Rx = aspect_x * R, Ry = aspect_y * R;
				int ofs2_x = offset_xi - blur_xi, ofs2_y = offset_yi - blur_yi,
					w2 = width_dst, h2 = height_dst;
				if (i < inf_def_num - 1) {
					ofs2_x = static_cast<int>(std::ceil(aspect_x * r)) + ofs1_x;
					ofs2_y = static_cast<int>(std::ceil(aspect_y * r)) + ofs1_y;
					D3D::clamp_extension_2d(ofs2_x, width_src);
					D3D::clamp_extension_2d(ofs2_y, height_src);
					w2 = width_src + 2 * ofs2_x;
					h2 = height_src + 2 * ofs2_y;
				}

				uint32_t arc_length, mid_width, mid_height;
				max::buff_spec::get_size_arc(Rx, Ry, arc_length);
				max::buff_spec::get_size_mid(Rx, Ry, superellipse_exp,
					width_src + 2 * ofs1_x, height_src + 2 * ofs1_y, w2, h2,
					mid_width, mid_height);
				ofs1_x = ofs2_x; ofs1_y = ofs2_y;

				max_arc_length = std::max(max_arc_length, arc_length);
				max_mid_width = std::max(max_mid_width, mid_width);
				max_mid_height = std::max(max_mid_height, mid_height);
			}
			auto arc = D3D::create_structured_buffer(max::buff_spec::elem_size_arc, max_arc_length);
			if (arc == nullptr) return nullptr;
			auto mid = D3D::create_texture(max::buff_spec::format_mid, max_mid_width, max_mid_height);
			if (mid == nullptr) return nullptr;
			auto uav_arc = D3D::to_unordered_access_view(arc.Get());
			if (uav_arc == nullptr) return nullptr;
			auto srv_arc = D3D::to_shader_resource_view(arc.Get());
			if (srv_arc == nullptr) return nullptr;
			auto uav_mid = D3D::to_unordered_access_view(mid.Get());
			if (uav_mid == nullptr) return nullptr;
			auto srv_mid = D3D::to_shader_resource_view(mid.Get());
			if (srv_mid == nullptr) return nullptr;

			// apply inflation and deflation sequentially.
			for (int i = 0, ofs1_x = 0, ofs1_y = 0; i < inf_def_num; i++) {
				auto const& r = inf_def_seq[i];
				auto const R = std::abs(r), Rx = aspect_x * R, Ry = aspect_y * R,
					rx = aspect_x * r, ry = aspect_y * r;
				int ofs2_x = offset_xi - blur_xi,
					ofs2_y = offset_yi - blur_yi,
					w2 = width_dst, h2 = height_dst;
				double dx = delta_x, dy = delta_y;
				if (i < inf_def_num - 1) {
					ofs2_x = static_cast<int>(std::ceil(rx)) + ofs1_x;
					ofs2_y = static_cast<int>(std::ceil(ry)) + ofs1_y;
					D3D::clamp_extension_2d(ofs2_x, width_src);
					D3D::clamp_extension_2d(ofs2_y, height_src);
					w2 = width_src + 2 * ofs2_x;
					h2 = height_src + 2 * ofs2_y;
					dx = 0; dy = 0;
				}

				if (!max::inflate(r < 0,
					width_src + 2 * ofs1_x, height_src + 2 * ofs1_y, w2, h2,
					ofs2_x - ofs1_x, ofs2_y - ofs1_y, dx, dy,
					srv_shapes[idx_curr_shape_src].Get(), uav_shapes[idx_curr_shape_src ^ 1].Get(),
					Rx, Ry, superellipse_exp,
					{ srv_arc.Get(), uav_arc.Get() }, { srv_mid.Get(), uav_mid.Get() })) return nullptr;

				ofs1_x = ofs2_x; ofs1_y = ofs2_y;
				idx_curr_shape_src ^= 1;
			}
			break;
		}
		default: return nullptr;
		}
	}

	// apply blur.
	if (blur > 0) {
		if (!ops::blur(
			width_dst - 2 * blur_xi, height_dst - 2 * blur_yi,
			{ srv_shapes[idx_curr_shape_src].Get(), uav_shapes[idx_curr_shape_src].Get() },
			{ srv_shapes[idx_curr_shape_src ^ 1].Get(), uav_shapes[idx_curr_shape_src ^ 1].Get() },
			aspect_x * blur, aspect_y * blur)) return nullptr;
	}

	return srv_shapes[idx_curr_shape_src];
}
