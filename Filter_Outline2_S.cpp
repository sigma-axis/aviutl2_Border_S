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
#include "image_ops.hpp"
using color_float = Border_S::image_ops::color_float;
using image_ops = Border_S::image_ops::ops;

#include "Filters_Common.hpp"
namespace common = Border_S::Filter::common;
#include "Filter_Outline2_S.hpp"

#include "Border_S.hpp"

#define ANON_NS_B namespace {
#define ANON_NS_E }


ANON_NS_B
////////////////////////////////
// setting parameters.
////////////////////////////////
namespace params
{
	FILTER_ITEM_TRACK distance{ L"距離", 10.00, -500.00, 500.00, 0.01 };
	FILTER_ITEM_TRACK line{ L"ライン幅", 10.00, -4000.00, 500.00, 0.01 };
	FILTER_ITEM_TRACK blur{ L"ぼかし", 0.00, 0.00, 500.00, 0.01 };
	FILTER_ITEM_COLOR color{ L"縁色", 0xff'ff'ff }; // defaults white.
	using methods = common::methods;
	FILTER_ITEM_SELECT method{ L"方式", methods::bin_smooth, const_cast<FILTER_ITEM_SELECT::ITEM*>(methods::items) };
	FILTER_ITEM_TRACK a_param{ L"α調整", 50.00, 0.00, 100.00, 0.01 };

	FILTER_ITEM_GROUP group_composite{ L"合成", false };
	struct compositions {
		enum id : int {
			background = 0,
			foreground = 1,
		};
		constexpr static id clamp(int value) { return static_cast<id>(std::min(std::max(value, 0), 1)); }
		constexpr static FILTER_ITEM_SELECT::ITEM items[] = {
			{ L"背面", background },
			{ L"前面", foreground },
			{ nullptr, {} },
		};
	};
	FILTER_ITEM_SELECT composition{ L"ライン配置", compositions::background, const_cast<FILTER_ITEM_SELECT::ITEM*>(compositions::items) };
	FILTER_ITEM_TRACK alpha_border{ L"ライン透明度", 0.00, 0.00, 100.00, 0.01 };
	FILTER_ITEM_TRACK alpha_inner{ L"内側透明度", 100.00, 0.00, 100.00, 0.01 };
	FILTER_ITEM_TRACK alpha_source{ L"元画像透明度", 100.00, 0.00, 100.00, 0.01 };

	FILTER_ITEM_GROUP group_move{ L"位置調整", false };
	FILTER_ITEM_TRACK move_x{ L"移動X", 0.00, -1000.00, +1000.00, 0.01 };
	FILTER_ITEM_TRACK move_y{ L"移動Y", 0.00, -1000.00, +1000.00, 0.01 };

	FILTER_ITEM_GROUP group_others{ L"その他", false };
	FILTER_ITEM_TRACK dist_aspect{ L"距離縦横比", 0.000, -100.000, +100.000, 0.001 };
	FILTER_ITEM_TRACK pos_radius{ L"凸半径", 0.00, 0.00, 500.00, 0.01 };
	FILTER_ITEM_TRACK neg_radius{ L"凹半径", 0.00, 0.00, 500.00, 0.01 };
	FILTER_ITEM_TRACK dist_sup_ell_expo{ L"距離膨らみ", 100.000, -300.000, +300.00, 0.001 };
	FILTER_ITEM_TRACK line_aspect{ L"ライン縦横比", 0.000, -100.000, +100.000, 0.001 };
	FILTER_ITEM_TRACK line_sup_ell_expo{ L"ライン膨らみ", 100.000, -300.000, +300.00, 0.001 };
	struct directions {
		enum id : int {
			outer = 0,
			inner = 1,
			faster = 2,
		};
		constexpr static id clamp(int value) { return static_cast<id>(std::min(std::max(value, 0), 2)); }
		constexpr static FILTER_ITEM_SELECT::ITEM items[] = {
			{ L"外側優先", outer },
			{ L"内側優先", inner },
			{ L"速い方", faster },
			{ nullptr, {} },
		};
	};
	FILTER_ITEM_SELECT direction{ L"手順", directions::outer, const_cast<FILTER_ITEM_SELECT::ITEM*>(directions::items) };

	constexpr void* all[] = {
		&distance,
		&line,
		&blur,
		&color,
		&method,
		&a_param,

		&group_composite,
		&composition,
		&alpha_border,
		&alpha_inner,
		&alpha_source,

		//&group_move,
		//&move_x,
		//&move_y,

		&group_others,
		&dist_aspect,
		&pos_radius,
		&neg_radius,
		&dist_sup_ell_expo,
		&line_aspect,
		&line_sup_ell_expo,
		&direction,

		nullptr,
	};
}

////////////////////////////////
// filter functions.
////////////////////////////////
bool apply_blur(int width, int height, double blur_x, double blur_y,
	::ID3D11Texture2D* src, D3D::cs_views const& src_views,
	::ID3D11Texture2D* dst, D3D::cs_views const& dst_views)
{
	int dxi = -static_cast<int>(std::ceil(blur_x)),
		dyi = -static_cast<int>(std::ceil(blur_y));
	D3D::clamp_extension_2d(dxi, width);
	D3D::clamp_extension_2d(dyi, height);
	int width_src = width + 2 * dxi, height_src = height + 2 * dyi;

	// move the image for the blur effect.
	::D3D11_BOX const box = {
		.left = static_cast<uint32_t>(-dxi),
		.top = static_cast<uint32_t>(-dyi),
		.front = 0,
		.right = static_cast<uint32_t>(width + dxi),
		.bottom = static_cast<uint32_t>(height + dyi),
		.back = 1,
	};
	D3D::cxt->CopySubresourceRegion(dst, 0, 0, 0, 0, src, 0, &box);

	// apply blur.
	return image_ops::blur(width_src, height_src,
		dst_views, src_views, blur_x, blur_y);
}

bool filter_core(
	double distance, double pos_radius, double neg_radius, double line, double blur,
	double d_aspect_x, double d_aspect_y, double d_superellipse_expo,
	double l_aspect_x, double l_aspect_y, double l_superellipse_expo,
	double move_x, double move_y,
	params::methods::id method, double a_param,
	params::compositions::id composition, double alpha_border, double alpha_inner, double alpha_source,
	color_float const& color, params::directions::id direction,
	FILTER_PROC_VIDEO* video)
{
	// determine the input and output dimensions.
	int const width_src = video->object->width, height_src = video->object->height;
	int size_li = std::max(static_cast<int>(std::ceil(d_aspect_x * distance + l_aspect_x * std::max(line, 0.0) - move_x)), 0),
		size_ti = std::max(static_cast<int>(std::ceil(d_aspect_y * distance + l_aspect_y * std::max(line, 0.0) - move_y)), 0),
		size_ri = std::max(static_cast<int>(std::ceil(d_aspect_x * distance + l_aspect_x * std::max(line, 0.0) + move_x)), 0),
		size_bi = std::max(static_cast<int>(std::ceil(d_aspect_y * distance + l_aspect_y * std::max(line, 0.0) + move_y)), 0);
	D3D::clamp_extension_2d(size_li, size_ri, width_src);
	D3D::clamp_extension_2d(size_ti, size_bi, height_src);
	int const width_dst = width_src + size_li + size_ri, height_dst = height_src + size_ti + size_bi;

	// determine the blur size.
	bool const has_hole =
		width_src + 2 * (d_aspect_x * distance + l_aspect_x * line) > 0 &&
		height_src + 2 * (d_aspect_y * distance + l_aspect_y * line) > 0;
	double adj_distance = distance, adj_line = line, adj_blur = blur,
		pre_infl_x = 0, pre_infl_y = 0;
	adj_blur = std::min(adj_blur, std::abs(adj_line) / 4);
	if (l_aspect_x > 0) adj_blur = std::min(adj_blur, std::max((width_src - d_aspect_x * distance / 2) / l_aspect_x, 0.0));
	if (l_aspect_y > 0) adj_blur = std::min(adj_blur, std::max((height_src - d_aspect_y * distance / 2) / l_aspect_y, 0.0));
	if (adj_blur != 0) {
		// adjust distance and the line width, taking account of the blur size.
		auto const dl = adj_line > 0 ? adj_blur : -adj_blur,
			dd = dl * std::min(
				d_aspect_x > 0 ? l_aspect_x / d_aspect_x : 1,
				d_aspect_y > 0 ? l_aspect_y / d_aspect_y : 1);

		adj_line -= 2 * dl;
		adj_distance += dd;
		pre_infl_x = l_aspect_x * dl - d_aspect_x * dd;
		pre_infl_y = l_aspect_y * dl - d_aspect_y * dd;

		// adjust possible floating point errors. either one of the two must be zero.
		(std::abs(pre_infl_x) < std::abs(pre_infl_y) ? pre_infl_x : pre_infl_y) = 0;
	}

	// determine inflation/deflation sequence.
	int inf_def_num = 0;
	double inf_def_seq[3];
	{
		auto const append_inf_def = [&](double r) -> void {
			if (r == 0) return;
			else if (inf_def_num > 0 &&
				(r > 0) == (inf_def_seq[inf_def_num - 1] > 0))
				inf_def_seq[inf_def_num - 1] += r;
			else inf_def_seq[inf_def_num++] = r;
		};
		double const d = adj_distance,
			p = std::max(pos_radius - adj_blur, 0.0),
			n = std::max(neg_radius - adj_blur, 0.0);
		if (direction == params::directions::outer ||
			(direction == params::directions::faster && d >= 0)) {
			append_inf_def(std::min(0.0, d - p));
			append_inf_def(std::max(d, p) + n);
			append_inf_def(-n);
		}
		else {
			append_inf_def(std::max(0.0, d + n));
			append_inf_def(std::min(d, -n) - p);
			append_inf_def(p);
		}
	}

	// image operations.
	auto obj = video->get_image_texture2d();
	if (!D3D::init(obj)) return false;

	D3D::ComPtr<::ID3D11Texture2D> src_obj = obj;
	if (alpha_source > 0) {
		src_obj = D3D::clone(obj);
		if (src_obj == nullptr) return false;
	}

	// prepare views.
	auto srv_src_obj = D3D::to_shader_resource_view(src_obj.Get());
	if (srv_src_obj == nullptr) return false;

	// apply the sequence of inflation/deflation.
	D3D::ComPtr<::ID3D11ShaderResourceView> srv_shape;
	if (pre_infl_x != 0 || pre_infl_y != 0) {
		double const
			size_pre_infl = std::abs(pre_infl_x) >= std::abs(pre_infl_y) ? pre_infl_x : pre_infl_y,
			pre_aspect_x = std::clamp(std::abs(pre_infl_x / size_pre_infl), 0.0, 1.0),
			pre_aspect_y = std::clamp(std::abs(pre_infl_y / size_pre_infl), 0.0, 1.0);
		// re-calculate pre_infl_x and pre_infl_y.
		pre_infl_x = pre_aspect_x * size_pre_infl;
		pre_infl_y = pre_aspect_y * size_pre_infl;
		int const
			pre_infl_xi = static_cast<int>(std::ceil(pre_infl_x)),
			pre_infl_yi = static_cast<int>(std::ceil(pre_infl_y));

		// pre-inflation/deflation to adjust the mismatching aspect ratios.
		auto srv_pre = common::sequential_inf_def(
			width_src, height_src, width_dst, height_dst, pre_infl_xi, pre_infl_yi,
			srv_src_obj.Get(), false,
			&size_pre_infl, 1, 0,
			pre_aspect_x, pre_aspect_y, 0 /* cross shape */,
			params::methods::intermed_method(method), a_param);
		if (srv_pre == nullptr) return false;

		// main inflation/deflation.
		srv_shape = common::sequential_inf_def(
			width_src + 2 * pre_infl_xi, height_src + 2 * pre_infl_yi, width_dst, height_dst,
			size_li + move_x - pre_infl_xi, size_ti + move_y - pre_infl_yi,
			srv_pre.Get(), true,
			inf_def_seq, inf_def_num, 0,
			d_aspect_x, d_aspect_y, d_superellipse_expo,
			method, params::methods::second_param_a(a_param, method));
		if (srv_shape == nullptr) return false;

		if (!has_hole && adj_blur > 0) {
			// prepare views for the buffers.
			auto shape = D3D::get_resource<::ID3D11Texture2D>(srv_shape.Get());
			if (shape == nullptr) return false;
			auto uav_shape = D3D::to_unordered_access_view(shape.Get());
			if (uav_shape == nullptr) return false;
			auto pre = D3D::get_resource<::ID3D11Texture2D>(srv_pre.Get());
			if (pre == nullptr) return false;
			auto uav_pre = D3D::to_unordered_access_view(pre.Get());
			if (uav_pre == nullptr) return false;

			if (!apply_blur(width_dst, height_dst, l_aspect_x * adj_blur, l_aspect_y * adj_blur,
				shape.Get(), { srv_shape.Get(), uav_shape.Get() },
				pre.Get(), { srv_pre.Get(), uav_pre.Get() })) return false;
			srv_shape.Swap(srv_pre);
		}
	}
	else {
		// this case, either blur is zero, or the two aspect ratios are the same.
		srv_shape = common::sequential_inf_def(
			width_src, height_src, width_dst, height_dst, size_li + move_x, size_ti + move_y,
			srv_src_obj.Get(), false,
			inf_def_seq, inf_def_num, has_hole ? 0 : adj_blur,
			d_aspect_x, d_aspect_y, d_superellipse_expo,
			method, a_param);
		if (srv_shape == nullptr) return false;
	}

	if (has_hole) {
		// the second edge at the other side of the band.
		auto srv_hole = common::sequential_inf_def(
			width_dst, height_dst, width_dst, height_dst, 0, 0,
			srv_shape.Get(), true,
			&adj_line, 1, 0,
			l_aspect_x, l_aspect_y, l_superellipse_expo,
			method, params::methods::second_param_a(a_param, method));
		if (srv_hole == nullptr) return false;
		if (adj_line >= 0) srv_shape.Swap(srv_hole); // `shape` is now outer, `hole` is now inner.

		// prepare a view for the buffer `shape`.
		auto shape = D3D::get_resource<::ID3D11Texture2D>(srv_shape.Get());
		if (shape == nullptr) return false;
		auto uav_shape = D3D::to_unordered_access_view(shape.Get());
		if (uav_shape == nullptr) return false;

		// take the difference of the two shapes.
		if (alpha_inner < 1 &&
			!image_ops::carve(width_dst, height_dst, width_dst, height_dst, 0, 0,
				srv_hole.Get(), uav_shape.Get(), alpha_inner - 1, true)) return false;

		// process blur.
		if (adj_blur > 0) {
			// prepare a view for the buffer `hole`.
			auto hole = D3D::get_resource<::ID3D11Texture2D>(srv_hole.Get());
			if (hole == nullptr) return false;
			auto uav_hole = D3D::to_unordered_access_view(hole.Get());
			if (uav_hole == nullptr) return false;

			if (!apply_blur(width_dst, height_dst, l_aspect_x * adj_blur, l_aspect_y * adj_blur,
				shape.Get(), { srv_shape.Get(), uav_shape.Get() },
				hole.Get(), { srv_hole.Get(), uav_hole.Get() })) return false;
			srv_shape.Swap(srv_hole);
		}
	}

	// combine with the original image.
	if (alpha_source > 0) {
		int const
			diff_li = std::max(0, size_li), diff_ti = std::max(0, size_ti),
			diff_ri = std::max(0, size_ri), diff_bi = std::max(0, size_bi);
		int const width = width_src + diff_li + diff_ri, height = height_src + diff_ti + diff_bi;

		// prepare the new buffer.
		video->set_image_data(nullptr, width, height);
		obj = video->get_image_texture2d();
		auto uav_obj = D3D::to_unordered_access_view(obj);
		if (uav_obj == nullptr) return false;
		if (!image_ops::combine(
			width_src, height_src,
			width_dst, height_dst,
			width, height,
			diff_li, diff_ti,
			diff_li - size_li, diff_ti - size_ti,
			srv_src_obj.Get(), srv_shape.Get(),
			uav_obj.Get(),
			color, alpha_source, alpha_border,
			composition == params::compositions::background)) return false;

		// TODO: handle `obj.cx` and `obj.cy`.
		//   equivalent to: obj.cx += (diff_li - diff_ri) / 2; obj.cy += (diff_ti - diff_bi) / 2;
	}
	else {
		if (width_dst != width_src || height_dst != height_src) {
			// prepare the new buffer.
			video->set_image_data(nullptr, width_dst, height_dst);
			obj = video->get_image_texture2d();
		}

		auto uav_obj = D3D::to_unordered_access_view(obj);
		if (uav_obj == nullptr) return false;
		float const col[] = {
			static_cast<float>(alpha_border * color.r),
			static_cast<float>(alpha_border * color.g),
			static_cast<float>(alpha_border * color.b),
			static_cast<float>(alpha_border * color.a),
		};
		D3D::cxt->ClearUnorderedAccessViewFloat(uav_obj.Get(), col);
		if (!image_ops::carve(width_dst, height_dst, width_dst, height_dst, 0, 0,
			srv_shape.Get(), uav_obj.Get(), 1, false)) return false;

		// TODO: handle `obj.cx` and `obj.cy`.
		//   equivalent to: obj.cx += (size_li - size_ri) / 2; obj.cy += (size_ti - size_bi) / 2;
	}

	return true;
}


////////////////////////////////
// entry point.
////////////////////////////////
bool filter(FILTER_PROC_VIDEO* video)
{
	// take parameters.
	double const
		distance = std::min(std::max(params::distance.value, params::distance.s), params::distance.e),
		line = std::min(std::max(params::line.value, params::line.s), params::line.e),
		blur = std::min(std::max(params::blur.value, params::blur.s), params::blur.e),
		a_param = std::min(std::max(params::a_param.value, params::a_param.s), params::a_param.e) / 100,

		alpha_border = 1 - std::min(std::max(params::alpha_border.value, params::alpha_border.s), params::alpha_border.e) / 100,
		alpha_inner = 1 - std::min(std::max(params::alpha_inner.value, params::alpha_inner.s), params::alpha_inner.e) / 100,
		alpha_source = 1 - std::min(std::max(params::alpha_source.value, params::alpha_source.s), params::alpha_source.e) / 100,

		//move_x = std::min(std::max(params::move_x.value, params::move_x.s), params::move_x.e),
		//move_y = std::min(std::max(params::move_y.value, params::move_y.s), params::move_y.e),
		move_x = 0, move_y = 0,

		dist_aspect = std::min(std::max(params::dist_aspect.value, params::dist_aspect.s), params::dist_aspect.e) / 100,
		dist_sup_ell_expo = common::conv_sup_ell_expo(
			std::min(std::max(params::dist_sup_ell_expo.value, params::dist_sup_ell_expo.s), params::dist_sup_ell_expo.e) / 100),
		pos_radius = std::min(std::max(params::pos_radius.value, params::pos_radius.s), params::pos_radius.e),
		neg_radius = std::min(std::max(params::neg_radius.value, params::neg_radius.s), params::neg_radius.e),
		line_aspect = std::min(std::max(params::line_aspect.value, params::line_aspect.s), params::line_aspect.e) / 100,
		line_sup_ell_expo = common::conv_sup_ell_expo(
			std::min(std::max(params::line_sup_ell_expo.value, params::line_sup_ell_expo.s), params::line_sup_ell_expo.e) / 100);
	auto const method = params::methods::clamp(params::method.value);
	auto const composition = params::compositions::clamp(params::composition.value);
	auto const direction = params::directions::clamp(params::direction.value);
	auto const color = color_float::from_rgb(params::color.value.code & 0xffffff);

	// further calculations.
	double const
		alpha_border2 = line != 0 ? alpha_border : alpha_inner,
		// sup_ell_expo < 1 is because the shape no longer satisfies the triangle inequality.
		line2 = line != 0 && (alpha_inner < 1 ||
			dist_aspect != line_aspect ||
			dist_sup_ell_expo != line_sup_ell_expo ||
			dist_sup_ell_expo < 1) ? line : params::line.s,
		line3 = line2 >= -2 * params::line.e ? line2 : params::line.s,
		distance2 = line > 0 && line3 < 0 ? distance + line : distance,
		d_aspect_x = std::min(1.0, 1 - dist_aspect),
		d_aspect_y = std::min(1.0, 1 + dist_aspect),
		l_aspect_x = std::min(1.0, 1 - line_aspect),
		l_aspect_y = std::min(1.0, 1 + line_aspect),
		blur2 = std::min(blur, std::abs(line) / 2);

	// handle trivial cases.
	if (video->object->width + d_aspect_x * distance2 <= 0 ||
		video->object->height + d_aspect_y * distance2 <= 0 ||
		line3 == 0 || alpha_border2 <= 0) {

		// find the extension size.
		int const
			size_xi = std::min(std::max(
				static_cast<int>(std::ceil(d_aspect_x * distance2 + l_aspect_x * std::max(line3, 0.0))), 0),
				static_cast<int>(D3D::max_image_size - video->object->width) >> 1),
			size_yi = std::min(std::max(
				static_cast<int>(std::ceil(d_aspect_y * distance2 + l_aspect_y * std::max(line3, 0.0))), 0),
				static_cast<int>(D3D::max_image_size - video->object->height) >> 1);
		if (size_xi <= 0 && size_yi <= 0 && alpha_source >= 1) return true; // nothing to do.

		auto obj = video->get_image_texture2d();
		if (!D3D::init(obj)) return false;

		// push alpha value.
		if (alpha_source < 1) {
			auto uav_obj = D3D::to_unordered_access_view(obj);
			if (uav_obj == nullptr) return false;
			if (!image_ops::push_alpha(video->object->width, video->object->height,
				uav_obj.Get(), alpha_source)) return false;
		}

		// extend the margin.
		if (size_xi > 0 || size_yi > 0) {
			if (!common::add_size(size_xi, size_yi, video)) return false;
		}
		return true;
	}

	return filter_core(
		distance2, pos_radius, neg_radius, line3, blur2 / 2,
		d_aspect_x, d_aspect_y, dist_sup_ell_expo,
		l_aspect_x, l_aspect_y, line_sup_ell_expo,
		move_x, move_y,
		method, a_param,
		composition, alpha_border2, alpha_inner, alpha_source,
		color, direction,
		video);
}
ANON_NS_E


////////////////////////////////
// exports.
////////////////////////////////
#define FILTER_NAME	L"アウトラインσ"
constinit FILTER_PLUGIN_TABLE Border_S::Filter::Outline2_S::table{
	.flag = FILTER_PLUGIN_TABLE::FLAG_VIDEO,
	.name = FILTER_NAME,
	.label = FILTER_LABEL_FMT(L"加工"),
	.information = PLUGIN_INFO_FMT(FILTER_NAME, PLUGIN_VERSION, PLUGIN_AUTHOR),
	.items = const_cast<void**>(params::all),
	.func_proc_video = &filter,
};
