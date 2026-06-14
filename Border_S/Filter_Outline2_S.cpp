/*
The MIT License (MIT)

Copyright (c) 2026 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <cstdint>
#include <cmath>
#include <numbers>
#include <algorithm>

#include "d3d_service.hpp"
using d3d_service::D3D;
#include "image_ops.hpp"
using Border_S::image_ops::color_float;
using pattern_ops = Border_S::image_ops::pattern_info;
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
	FILTER_ITEM_TRACK distance{ L"距離", 10.00, -500.00, 500.00, 0.01, nullptr, 0.4 };
	FILTER_ITEM_TRACK line{ L"ライン幅", 10.00, -4000.00, 500.00, 0.01, nullptr, 0.4 };
	FILTER_ITEM_TRACK blur{ L"ぼかし", 0.00, 0.00, 500.00, 0.01, nullptr, 0.4 };
	FILTER_ITEM_COLOR color{ L"縁色", 0xff'ff'ff }; // defaults white.
	using common::methods;
	FILTER_ITEM_SELECT method{ L"方式", methods::bin_smooth, const_cast<FILTER_ITEM_SELECT::ITEM*>(methods::items) };
	FILTER_ITEM_TRACK a_param{ L"α調整", 50.00, 0.00, 100.00, 0.01 };

	FILTER_ITEM_GROUP group_composite{ L"合成", false };
	struct compositions {
		enum id : int {
			background = 0,
			foreground = 1,
		};
		constexpr static id clamp(int value) { return static_cast<id>(std::clamp(value, 0, 1)); }
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
	FILTER_ITEM_TRACK move_x{ L"移動X", 0.00, -1000.00, +1000.00, 0.01, nullptr, 0.5 };
	FILTER_ITEM_TRACK move_y{ L"移動Y", 0.00, -1000.00, +1000.00, 0.01, nullptr, 0.5 };

	FILTER_ITEM_GROUP group_pattern{ L"パターン画像", false };
	using common::pattern_types;
	FILTER_ITEM_SELECT pattern_type{ L"pattern::パターンの種類", pattern_types::none, const_cast<FILTER_ITEM_SELECT::ITEM*>(pattern_types::items) };
	FILTER_ITEM_FILE pattern_file{ L"pattern::画像ファイル", L"", pattern_types::file_filter };
	FILTER_ITEM_TRACK pattern_x{ L"pattern::移動X", 0.00, -4000.00, +4000.00, 0.01, nullptr, 0.25 };
	FILTER_ITEM_TRACK pattern_y{ L"pattern::移動Y", 0.00, -4000.00, +4000.00, 0.01, nullptr, 0.25 };
	FILTER_ITEM_TRACK pattern_scale{ L"pattern::拡大率", 100.00, 0.001, 10000.00, 0.001, nullptr, 0.02 };
	FILTER_ITEM_TRACK pattern_rotate{ L"pattern::回転", 0.00, -1440.00, +1440.00, 0.01, nullptr, 0.25 };
	using common::pattern_origins;
	FILTER_ITEM_SELECT pattern_origin{ L"pattern::基準位置", pattern_origins::shape, const_cast<FILTER_ITEM_SELECT::ITEM*>(pattern_origins::items) };
	FILTER_ITEM_CHECK pattern_snap_to_pixel{ L"pattern::補間なし", true };

	FILTER_ITEM_GROUP group_others{ L"その他", false };
	FILTER_ITEM_TRACK dist_aspect{ L"距離縦横比", 0.000, -100.000, +100.000, 0.001 };
	FILTER_ITEM_TRACK pos_radius{ L"凸半径", 0.00, 0.00, 500.00, 0.01, nullptr, 0.4 };
	FILTER_ITEM_TRACK neg_radius{ L"凹半径", 0.00, 0.00, 500.00, 0.01, nullptr, 0.4 };
	FILTER_ITEM_TRACK dist_sup_ell_expo{ L"距離膨らみ", 100.000, -300.000, +300.00, 0.001 };
	FILTER_ITEM_TRACK line_aspect{ L"ライン縦横比", 0.000, -100.000, +100.000, 0.001 };
	FILTER_ITEM_TRACK line_sup_ell_expo{ L"ライン膨らみ", 100.000, -300.000, +300.00, 0.001 };
	struct directions {
		enum id : int {
			outer = 0,
			inner = 1,
			faster = 2,
		};
		constexpr static id clamp(int value) { return static_cast<id>(std::clamp(value, 0, 2)); }
		constexpr static FILTER_ITEM_SELECT::ITEM items[] = {
			{ L"外側優先", outer },
			{ L"内側優先", inner },
			{ L"速い方", faster },
			{ nullptr, {} },
		};
	};
	FILTER_ITEM_SELECT direction{ L"手順", directions::outer, const_cast<FILTER_ITEM_SELECT::ITEM*>(directions::items) };
	using blur_spec = common::blur;
	FILTER_ITEM_SELECT blur_type{ L"ぼかしの種類", common::blur::triangular, const_cast<FILTER_ITEM_SELECT::ITEM*>(common::blur::items) };

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

		&group_move,
		&move_x,
		&move_y,

		&group_pattern,
		&pattern_type,
		&pattern_file,
		&pattern_x,
		&pattern_y,
		&pattern_rotate,
		&pattern_scale,
		&pattern_origin,
		&pattern_snap_to_pixel,

		&group_others,
		&dist_aspect,
		&pos_radius,
		&neg_radius,
		&dist_sup_ell_expo,
		&line_aspect,
		&line_sup_ell_expo,
		&direction,
		&blur_type,

		nullptr,
	};
}

////////////////////////////////
// filter functions.
////////////////////////////////
inline image_ops::blur_type conv(common::blur::id type)
{
	switch (type) {
	default:
	case common::blur::triangular: return image_ops::blur_type::triangular;
	case common::blur::gaussian: return image_ops::blur_type::gaussian;
	}
}
bool apply_blur(int width, int height, double blur_x, double blur_y, params::blur_spec::id blur_type,
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
	return image_ops::blur(conv(blur_type),
		width_src, height_src, dst_views, src_views, blur_x, blur_y);
}

bool filter_core(
	double distance, double pos_radius, double neg_radius, double line, double blur,
	double d_aspect_x, double d_aspect_y, double d_superellipse_expo,
	double l_aspect_x, double l_aspect_y, double l_superellipse_expo,
	double move_x, double move_y,
	params::methods::id method, double a_param,
	params::compositions::id composition, double alpha_border, double alpha_inner, double alpha_source,
	color_float const& color, params::directions::id direction, params::blur_spec::id blur_type,
	common::pattern_info&& pattern,
	FILTER_PROC_VIDEO* video)
{
	if (pattern.failure) return false;

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
			&size_pre_infl, 1, 0, params::blur_spec::triangular,
			pre_aspect_x, pre_aspect_y, 0 /* cross shape */,
			params::methods::intermed_method(method), a_param);
		if (srv_pre == nullptr) return false;

		// main inflation/deflation.
		srv_shape = common::sequential_inf_def(
			width_src + 2 * pre_infl_xi, height_src + 2 * pre_infl_yi, width_dst, height_dst,
			size_li + move_x - pre_infl_xi, size_ti + move_y - pre_infl_yi,
			srv_pre.Get(), true,
			inf_def_seq, inf_def_num, 0, params::blur_spec::triangular,
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

			if (!apply_blur(width_dst, height_dst, l_aspect_x * adj_blur, l_aspect_y * adj_blur, blur_type,
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
			inf_def_seq, inf_def_num, has_hole ? 0 : adj_blur, blur_type,
			d_aspect_x, d_aspect_y, d_superellipse_expo,
			method, a_param);
		if (srv_shape == nullptr) return false;
	}

	if (has_hole) {
		// the second edge at the other side of the band.
		auto srv_hole = common::sequential_inf_def(
			width_dst, height_dst, width_dst, height_dst, 0, 0,
			srv_shape.Get(), true,
			&adj_line, 1, 0, params::blur_spec::triangular,
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

			if (!apply_blur(width_dst, height_dst, l_aspect_x * adj_blur, l_aspect_y * adj_blur, blur_type,
				shape.Get(), { srv_shape.Get(), uav_shape.Get() },
				hole.Get(), { srv_hole.Get(), uav_hole.Get() })) return false;
			srv_shape.Swap(srv_hole);
		}
	}
	// prepare pattern.
	D3D::ComPtr<::ID3D11ShaderResourceView> srv_pat = nullptr;
	pattern_ops pat_ops;
	if (pattern.is_valid()) {
		srv_pat = D3D::to_shader_resource_view(pattern.texture);
		if (srv_pat == nullptr) return false;
		pattern.move_to_shape(move_x, move_y);
		pat_ops = {
			.srv = srv_pat.Get(),
			.width = static_cast<int>(pattern.width),
			.height = static_cast<int>(pattern.height),
			.scale = pattern.scale,
			.rotate = pattern.rotate,
			.pos_x = pattern.pos_x,
			.pos_y = pattern.pos_y,
			.snap_to_pixel = pattern.snap_to_pixel,
		};
	}
	else pat_ops = { .solid = color };

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
			pat_ops, alpha_source, alpha_border,
			composition == params::compositions::background)) return false;

		// adjust center.
		video->param->cx += (diff_li - diff_ri) / 2.0f;
		video->param->cy += (diff_ti - diff_bi) / 2.0f;
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
		if (pattern.is_valid()){
			pat_ops.pos_x += (size_li - size_ri) / 2.0f;
			pat_ops.pos_y += (size_ti - size_bi) / 2.0f;
			if (!image_ops::recolor(0, 0, width_dst, height_dst, 0, 0,
				nullptr, uav_obj.Get(), pat_ops, true, 1, 0)) return false;
		}
		if (!image_ops::carve(width_dst, height_dst, width_dst, height_dst, 0, 0,
			srv_shape.Get(), uav_obj.Get(), 1, false)) return false;

		// adjust center.
		video->param->cx += (size_li - size_ri) / 2.0f;
		video->param->cy += (size_ti - size_bi) / 2.0f;
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
		distance = std::clamp(params::distance.value, params::distance.s, params::distance.e),
		line = std::clamp(params::line.value, params::line.s, params::line.e),
		blur = std::clamp(params::blur.value, params::blur.s, params::blur.e),
		a_param = std::clamp(params::a_param.value, params::a_param.s, params::a_param.e) / 100,

		alpha_border = 1 - std::clamp(params::alpha_border.value, params::alpha_border.s, params::alpha_border.e) / 100,
		alpha_inner = 1 - std::clamp(params::alpha_inner.value, params::alpha_inner.s, params::alpha_inner.e) / 100,
		alpha_source = 1 - std::clamp(params::alpha_source.value, params::alpha_source.s, params::alpha_source.e) / 100,

		move_x = std::clamp(params::move_x.value, params::move_x.s, params::move_x.e),
		move_y = std::clamp(params::move_y.value, params::move_y.s, params::move_y.e),

		pattern_x = std::clamp(params::pattern_x.value, params::pattern_x.s, params::pattern_x.e),
		pattern_y = std::clamp(params::pattern_y.value, params::pattern_y.s, params::pattern_y.e),
		pattern_rotate = std::fmod(std::clamp(params::pattern_rotate.value, params::pattern_rotate.s, params::pattern_rotate.e), 360) * std::numbers::pi / 180,
		pattern_scale = std::clamp(params::pattern_scale.value, params::pattern_scale.s, params::pattern_scale.e) / 100,

		dist_aspect = std::clamp(params::dist_aspect.value, params::dist_aspect.s, params::dist_aspect.e) / 100,
		dist_sup_ell_expo = common::conv_sup_ell_expo(
			std::clamp(params::dist_sup_ell_expo.value, params::dist_sup_ell_expo.s, params::dist_sup_ell_expo.e) / 100),
		pos_radius = std::clamp(params::pos_radius.value, params::pos_radius.s, params::pos_radius.e),
		neg_radius = std::clamp(params::neg_radius.value, params::neg_radius.s, params::neg_radius.e),
		line_aspect = std::clamp(params::line_aspect.value, params::line_aspect.s, params::line_aspect.e) / 100,
		line_sup_ell_expo = common::conv_sup_ell_expo(
			std::clamp(params::line_sup_ell_expo.value, params::line_sup_ell_expo.s, params::line_sup_ell_expo.e) / 100);
	auto const method = params::methods::clamp(params::method.value);
	auto const composition = params::compositions::clamp(params::composition.value);
	auto const direction = params::directions::clamp(params::direction.value);
	auto const blur_type = params::blur_spec::clamp(params::blur_type.value);
	auto const color = color_float::from_rgb(params::color.value.code & 0xffffff);
	auto const pattern_type = params::pattern_types::clamp(params::pattern_type.value);
	auto const pattern_file = params::pattern_file.value;
	auto const pattern_origin = params::pattern_origins::clamp(params::pattern_origin.value);
	auto const snap_to_pixel = params::pattern_snap_to_pixel.value;

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
	if (video->object->width + 2 * d_aspect_x * distance2 <= 0 ||
		video->object->height + 2 * d_aspect_y * distance2 <= 0 ||
		line3 == 0 || alpha_border2 <= 0) {

		// push alpha value.
		if (alpha_source < 1) {
			if (!common::push_alpha(alpha_source, video)) return false;
		}

		// extend the margin.
		int const
			size_li = std::max(static_cast<int>(
				std::ceil(d_aspect_x * distance2 + l_aspect_x * std::max(line3, 0.0) - move_x)), 0),
			size_ti = std::max(static_cast<int>(
				std::ceil(d_aspect_y * distance2 + l_aspect_y * std::max(line3, 0.0) - move_y)), 0),
			size_ri = std::max(static_cast<int>(
				std::ceil(d_aspect_x * distance2 + l_aspect_x * std::max(line3, 0.0) + move_x)), 0),
			size_bi = std::max(static_cast<int>(
				std::ceil(d_aspect_y * distance2 + l_aspect_y * std::max(line3, 0.0) + move_y)), 0);
		if (size_li > 0 || size_ti > 0 || size_ri > 0 || size_bi > 0) {
			if (!common::add_size(size_li, size_ti, size_ri, size_bi, video)) return false;
			video->param->cx += (size_li - size_ri) / 2.0f;
			video->param->cy += (size_ti - size_bi) / 2.0f;
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
		color, direction, blur_type,
		{
			pattern_type, pattern_file,
			pattern_scale, pattern_rotate, pattern_x, pattern_y,
			snap_to_pixel, pattern_origin,
			video,
		}, video);
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
