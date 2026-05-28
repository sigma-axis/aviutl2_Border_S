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
#include "Filter_Border2_S.hpp"

#include "Border_S.hpp"

#define ANON_NS_B namespace {
#define ANON_NS_E }


ANON_NS_B
////////////////////////////////
// setting parameters.
////////////////////////////////
namespace params
{
	FILTER_ITEM_TRACK size{ L"サイズ", 5.00, -500.00, 500.00, 0.01, nullptr, 0.4 };
	FILTER_ITEM_TRACK blur{ L"ぼかし", 0.00, 0.00, 200.00, 0.01, nullptr, 0.5 };
	FILTER_ITEM_COLOR color{ L"縁色", 0xff'ff'ff }; // defaults white.
	using common::methods;
	FILTER_ITEM_SELECT method{ L"方式", methods::bin_smooth, const_cast<FILTER_ITEM_SELECT::ITEM*>(methods::items) };
	FILTER_ITEM_TRACK a_param{ L"α調整", 50.00, 0.00, 100.00, 0.01 };
	struct directions {
		enum id : int {
			outer = 0,
			inner = 1,
		};
		constexpr static id clamp(int value) { return static_cast<id>(std::clamp(value, 0, 1)); }
		constexpr static FILTER_ITEM_SELECT::ITEM items[] = {
			{ L"外側縁取り", outer },
			{ L"内側縁取り", inner },
			{ nullptr, {} },
		};
	};
	FILTER_ITEM_SELECT direction{ L"タイプ", directions::outer, const_cast<FILTER_ITEM_SELECT::ITEM*>(directions::items) };

	FILTER_ITEM_GROUP group_alpha{ L"透明度設定", false };
	FILTER_ITEM_TRACK alpha_border{ L"透明度", 0.00, 0.00, 100.00, 0.01 };
	FILTER_ITEM_TRACK alpha_source{ L"前景透明度", 0.00, 0.00, 100.00, 0.01 };

	FILTER_ITEM_GROUP group_move{ L"位置調整", false };
	FILTER_ITEM_TRACK move_x{ L"移動X", 0.00, -1000.00, +1000.00, 0.01, nullptr, 0.5 };
	FILTER_ITEM_TRACK move_y{ L"移動Y", 0.00, -1000.00, +1000.00, 0.01, nullptr, 0.5 };

	FILTER_ITEM_GROUP group_pattern{ L"パターン画像", false };
	using common::pattern_types;
	FILTER_ITEM_SELECT pattern_type{ L"pattern::パターンの種類", pattern_types::none, const_cast<FILTER_ITEM_SELECT::ITEM*>(pattern_types::items) };
	FILTER_ITEM_FILE pattern_file{ L"pattern::画像ファイル", L"", L"Image File (*.bmp;*.tga;*.jpg;*.png;*.*)\0*.bmp;*.tga;*.jpg;*.png;*.*\0" };
	FILTER_ITEM_TRACK pattern_x{ L"pattern::移動X", 0.00, -4000.00, +4000.00, 0.01, nullptr, 0.25 };
	FILTER_ITEM_TRACK pattern_y{ L"pattern::移動Y", 0.00, -4000.00, +4000.00, 0.01, nullptr, 0.25 };
	FILTER_ITEM_TRACK pattern_rotate{ L"pattern::回転", 0.00, -1440.00, +1440.00, 0.01, nullptr, 0.25 };
	FILTER_ITEM_TRACK pattern_scale{ L"pattern::拡大率", 100.00, 0.001, 10000.00, 0.001, nullptr, 0.02 };
	using common::pattern_origins;
	FILTER_ITEM_SELECT pattern_origin{ L"pattern::基準位置", pattern_origins::shape, const_cast<FILTER_ITEM_SELECT::ITEM*>(pattern_origins::items) };
	FILTER_ITEM_CHECK pattern_snap_to_pixel{ L"pattern::補間なし", false };

	FILTER_ITEM_GROUP group_others{ L"その他", false };
	FILTER_ITEM_TRACK aspect{ L"縦横比", 0.000, -100.000, +100.000, 0.001 };
	FILTER_ITEM_TRACK pos_radius{ L"凸半径", 0.00, 0.00, 500.00, 0.01, nullptr, 0.4 };
	FILTER_ITEM_TRACK neg_radius{ L"凹半径", 0.00, 0.00, 500.00, 0.01, nullptr, 0.4 };
	FILTER_ITEM_TRACK sup_ell_expo{ L"膨らみ", 100.000, -300.000, +300.00, 0.001 };
	using blur_spec = common::blur;
	FILTER_ITEM_SELECT blur_type { L"ぼかしの種類", blur_spec::triangular, const_cast<FILTER_ITEM_SELECT::ITEM*>(common::blur::items) };

	constexpr void* all[] = {
		&size,
		&blur,
		&color,
		&method,
		&a_param,
		&direction,

		&group_alpha,
		&alpha_border,
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
		&aspect,
		&pos_radius,
		&neg_radius,
		&sup_ell_expo,
		&blur_type,

		nullptr,
	};
}

////////////////////////////////
// filter functions.
////////////////////////////////
bool filter_core(
	double size, double pos_radius, double neg_radius, double blur,
	double aspect_x, double aspect_y, double superellipse_exp,
	double move_x, double move_y,
	double alpha_border, double alpha_source,
	params::methods::id method, double a_param,
	color_float const& color, params::directions::id direction, params::blur_spec::id blur_type,
	common::pattern_info&& pattern,
	FILTER_PROC_VIDEO* video)
{
	// determine the input and output dimensions.
	int const width_src = video->object->width, height_src = video->object->height;
	int size_li, size_ti, size_ri, size_bi;
	switch (direction) {
	case params::directions::outer: default:
	{
		size_li = std::max(static_cast<int>(std::ceil(aspect_x * size - move_x)), 0);
		size_ti = std::max(static_cast<int>(std::ceil(aspect_y * size - move_y)), 0);
		size_ri = std::max(static_cast<int>(std::ceil(aspect_x * size + move_x)), 0);
		size_bi = std::max(static_cast<int>(std::ceil(aspect_y * size + move_y)), 0);
		break;
	}
	case params::directions::inner:
	{
		auto const bx = (size >= 0 ? 1 : -1) * aspect_x * blur, by = (size >= 0 ? 1 : -1) * aspect_y * blur;
		size_li = static_cast<int>(std::ceil(2 * bx)) + static_cast<int>(std::ceil(-aspect_x * size - move_x));
		size_ti = static_cast<int>(std::ceil(2 * by)) + static_cast<int>(std::ceil(-aspect_y * size - move_y));
		size_ri = static_cast<int>(std::ceil(2 * bx)) + static_cast<int>(std::ceil(-aspect_x * size + move_x));
		size_bi = static_cast<int>(std::ceil(2 * by)) + static_cast<int>(std::ceil(-aspect_y * size + move_y));
		break;
	}
	}
	D3D::clamp_extension_2d(size_li, size_ri, width_src);
	D3D::clamp_extension_2d(size_ti, size_bi, height_src);
	int const width_dst = width_src + size_li + size_ri, height_dst = height_src + size_ti + size_bi;

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
		double const d = size - (size >= 0 ? 1 : -1) * blur,
			p = std::max(pos_radius - blur, 0.0),
			n = std::max(neg_radius - blur, 0.0);
		append_inf_def(std::min(0.0, d - p));
		append_inf_def(std::max(d, p) + n);
		append_inf_def(-n);
	}
	if (direction == params::directions::inner) {
		for (int i = 0; i < inf_def_num; i++) inf_def_seq[i] *= -1;
	}

	// image operations.
	auto obj = video->get_image_texture2d();
	if (!D3D::init(obj)) return false;

	D3D::ComPtr<::ID3D11Texture2D> src_obj = obj;
	if (direction != params::directions::inner) {
		src_obj = D3D::clone(obj);
		if (src_obj == nullptr) return false;
		video->set_image_data(nullptr, width_dst, height_dst);
		obj = video->get_image_texture2d();
	}

	// prepare views.
	auto srv_src_obj = D3D::to_shader_resource_view(src_obj.Get());
	if (srv_src_obj == nullptr) return false;
	auto uav_obj = D3D::to_unordered_access_view(obj);
	if (uav_obj == nullptr) return false;

	// apply the sequence of inflation/deflation.
	auto srv_shape = common::sequential_inf_def(
		width_src, height_src, width_dst, height_dst, size_li + move_x, size_ti + move_y,
		srv_src_obj.Get(), false,
		inf_def_seq, inf_def_num, blur, blur_type,
		aspect_x, aspect_y, superellipse_exp,
		method, a_param);
	if (srv_shape == nullptr) return false;

	// prepare pattern.
	D3D::ComPtr<::ID3D11ShaderResourceView> srv_pat = nullptr;
	pattern_ops pat_ops;
	if (pattern.has_pattern()) {
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
	switch (direction) {
	case params::directions::outer: default:
	{
		
		if (!image_ops::draw(
			width_src, height_src,
			width_dst, height_dst,
			size_li, size_ti,
			srv_src_obj.Get(),
			srv_shape.Get(), uav_obj.Get(),
			pat_ops, alpha_source, alpha_border)) return false;

		// adjust center.
		video->param->cx += (size_li - size_ri) / 2.0f;
		video->param->cy += (size_ti - size_bi) / 2.0f;
		break;
	}
	case params::directions::inner:
	{
		if (!image_ops::recolor(
			width_dst, height_dst,
			width_src, height_src,
			-size_li, -size_ti,
			srv_shape.Get(), uav_obj.Get(),
			pat_ops, true, alpha_border, alpha_source)) return false;
		break;
	}
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
		size = std::clamp(params::size.value, params::size.s, params::size.e),
		blur = std::clamp(params::blur.value, params::blur.s, params::blur.e) / 100,
		a_param = std::clamp(params::a_param.value, params::a_param.s, params::a_param.e) / 100,

		alpha_border = 1 - std::clamp(params::alpha_border.value, params::alpha_border.s, params::alpha_border.e) / 100,
		alpha_source = 1 - std::clamp(params::alpha_source.value, params::alpha_source.s, params::alpha_source.e) / 100,

		move_x = std::clamp(params::move_x.value, params::move_x.s, params::move_x.e),
		move_y = std::clamp(params::move_y.value, params::move_y.s, params::move_y.e),

		pattern_x = std::clamp(params::pattern_x.value, params::pattern_x.s, params::pattern_x.e),
		pattern_y = std::clamp(params::pattern_y.value, params::pattern_y.s, params::pattern_y.e),
		pattern_rotate = std::fmod(std::clamp(params::pattern_rotate.value, params::pattern_rotate.s, params::pattern_rotate.e), 360) * std::numbers::pi / 180,
		pattern_scale = std::clamp(params::pattern_scale.value, params::pattern_scale.s, params::pattern_scale.e) / 100,

		aspect = std::clamp(params::aspect.value, params::aspect.s, params::aspect.e) / 100,
		pos_radius = std::clamp(params::pos_radius.value, params::pos_radius.s, params::pos_radius.e),
		neg_radius = std::clamp(params::neg_radius.value, params::neg_radius.s, params::neg_radius.e),
		sup_ell_expo = common::conv_sup_ell_expo(
			std::clamp(params::sup_ell_expo.value, params::sup_ell_expo.s, params::sup_ell_expo.e) / 100);
	auto const method = params::methods::clamp(params::method.value);
	auto const direction = params::directions::clamp(params::direction.value);
	auto const blur_type = params::blur_spec::clamp(params::blur_type.value);
	auto const color = color_float::from_rgb(params::color.value.code & 0xffffff);
	auto const pattern_type = params::pattern_types::clamp(params::pattern_type.value);
	auto const pattern_file = params::pattern_file.value;
	auto const pattern_origin = params::pattern_origins::clamp(params::pattern_origin.value);
	auto const snap_to_pixel = params::pattern_snap_to_pixel.value;

	// further calculations.
	double const
		aspect_x = std::min(1.0, 1 - aspect),
		aspect_y = std::min(1.0, 1 + aspect);

	// handle trivial cases.
	if ((size <= 0 && pos_radius <= 0 && neg_radius <= 0 && move_x == 0 && move_y == 0) ||
		alpha_border == 0) {
		// push alpha value.
		if (alpha_source < 1) {
			if (!common::push_alpha(alpha_source, video)) return false;
		}

		// extend the margin.
		if (direction == params::directions::outer) {
			int const
				size_li = std::max(static_cast<int>(std::ceil(aspect_x * size - move_x)), 0),
				size_ti = std::max(static_cast<int>(std::ceil(aspect_y * size - move_y)), 0),
				size_ri = std::max(static_cast<int>(std::ceil(aspect_x * size + move_x)), 0),
				size_bi = std::max(static_cast<int>(std::ceil(aspect_y * size + move_y)), 0);
			if (size_li > 0 || size_ti > 0 || size_ri > 0 || size_bi > 0) {
				if (!common::add_size(size_li, size_ti, size_ri, size_bi, video)) return false;
				video->param->cx += (size_li - size_ri) / 2.0f;
				video->param->cy += (size_ti - size_bi) / 2.0f;
			}
		}
		return true;
	}

	return filter_core(size, pos_radius, neg_radius, std::max(0.0, std::abs(size) * blur / 2),
		aspect_x, aspect_y, sup_ell_expo,
		move_x, move_y,
		alpha_border, alpha_source,
		method, a_param,
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
#define FILTER_NAME	L"縁取りσ"
constinit FILTER_PLUGIN_TABLE Border_S::Filter::Border2_S::table{
	.flag = FILTER_PLUGIN_TABLE::FLAG_VIDEO,
	.name = FILTER_NAME,
	.label = FILTER_LABEL_FMT(L"装飾"),
	.information = PLUGIN_INFO_FMT(FILTER_NAME, PLUGIN_VERSION, PLUGIN_AUTHOR),
	.items = const_cast<void**>(params::all),
	.func_proc_video = &filter,
};
