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
	FILTER_ITEM_TRACK size{ L"サイズ", 5.00, 0.00, 500.00, 0.01 };
	FILTER_ITEM_TRACK blur{ L"ぼかし", 0.00, 0.00, 200.00, 0.01 };
	FILTER_ITEM_COLOR color{ L"縁色", 0xff'ff'ff }; // defaults white.
	using methods = common::methods;
	FILTER_ITEM_SELECT method{ L"方式", methods::bin_smooth, const_cast<FILTER_ITEM_SELECT::ITEM*>(methods::items) };
	FILTER_ITEM_TRACK a_param{ L"α調整", 50.00, 0.00, 100.00, 0.01 };
	struct directions {
		enum id : int {
			outer = 0,
			inner = 1,
		};
		constexpr static id clamp(int value) { return static_cast<id>(std::min(std::max(value, 0), 1)); }
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
	FILTER_ITEM_TRACK move_x{ L"移動X", 0.00, -1000.00, +1000.00, 0.01 };
	FILTER_ITEM_TRACK move_y{ L"移動Y", 0.00, -1000.00, +1000.00, 0.01 };

	FILTER_ITEM_GROUP group_others{ L"その他", false };
	FILTER_ITEM_TRACK aspect{ L"縦横比", 0.000, -100.000, +100.000, 0.001 };
	FILTER_ITEM_TRACK pos_radius{ L"凸半径", 0.00, 0.00, 500.00, 0.01 };
	FILTER_ITEM_TRACK neg_radius{ L"凹半径", 0.00, 0.00, 500.00, 0.01 };
	FILTER_ITEM_TRACK sup_ell_expo{ L"膨らみ", 100.000, -300.000, +300.00, 0.001 };

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

		//&group_move,
		//&move_x,
		//&move_y,

		&group_others,
		&aspect,
		&pos_radius,
		&neg_radius,
		&sup_ell_expo,

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
	color_float const& color, params::directions::id direction,
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
		auto const bx = aspect_x * blur, by = aspect_y * blur;
		size_li = static_cast<int>(std::ceil(bx)) + static_cast<int>(std::ceil(-aspect_x * size - move_x));
		size_ti = static_cast<int>(std::ceil(by)) + static_cast<int>(std::ceil(-aspect_y * size - move_y));
		size_ri = static_cast<int>(std::ceil(bx)) + static_cast<int>(std::ceil(-aspect_x * size + move_x));
		size_bi = static_cast<int>(std::ceil(by)) + static_cast<int>(std::ceil(-aspect_y * size + move_y));
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
		double const d = size - blur,
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
		inf_def_seq, inf_def_num, blur,
		aspect_x, aspect_y, superellipse_exp,
		method, a_param);
	if (srv_shape == nullptr) return false;

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
			color, alpha_source, alpha_border)) return false;

		// TODO: handle `obj.cx` and `obj.cy`.
		//   equivalent to: obj.cx += (size_li - size_ri) / 2; obj.cy += (size_ti - size_bi) / 2;
		break;
	}
	case params::directions::inner:
	{
		if (!image_ops::recolor(
			width_dst, height_dst,
			width_src, height_src,
			-size_li, -size_ti,
			srv_shape.Get(), uav_obj.Get(),
			color, true, alpha_source, alpha_border)) return false;
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
		size = std::min(std::max(params::size.value, params::size.s), params::size.e),
		blur = std::min(std::max(params::blur.value, params::blur.s), params::blur.e) / 100,
		a_param = std::min(std::max(params::a_param.value, params::a_param.s), params::a_param.e) / 100,

		alpha_border = 1 - std::min(std::max(params::alpha_border.value, params::alpha_border.s), params::alpha_border.e) / 100,
		alpha_source = 1 - std::min(std::max(params::alpha_source.value, params::alpha_source.s), params::alpha_source.e) / 100,

		//move_x = std::min(std::max(params::move_x.value, params::move_x.s), params::move_x.e),
		//move_y = std::min(std::max(params::move_y.value, params::move_y.s), params::move_y.e),
		move_x = 0, move_y = 0,

		aspect = std::min(std::max(params::aspect.value, params::aspect.s), params::aspect.e) / 100,
		pos_radius = std::min(std::max(params::pos_radius.value, params::pos_radius.s), params::pos_radius.e),
		neg_radius = std::min(std::max(params::neg_radius.value, params::neg_radius.s), params::neg_radius.e),
		sup_ell_expo = common::conv_sup_ell_expo(
			std::min(std::max(params::sup_ell_expo.value, params::sup_ell_expo.s), params::sup_ell_expo.e) / 100);
	auto const method = params::methods::clamp(params::method.value);
	auto const direction = params::directions::clamp(params::direction.value);
	auto const color = color_float::from_rgb(params::color.value.code & 0xffffff);

	// further calculations.
	double const
		aspect_x = std::min(1.0, 1 - aspect),
		aspect_y = std::min(1.0, 1 + aspect);

	// handle trivial cases.
	if (size == 0 && pos_radius == 0 && neg_radius == 0) {
		if (alpha_source < 1) {
			if (!common::push_alpha(alpha_source, video)) return false;
		}
		return true;
	}
	else if (alpha_border == 0) {
		if (direction == params::directions::outer && size > 0) {
			if (!common::add_size(
				static_cast<int>(std::ceil(aspect_x * size)),
				static_cast<int>(std::ceil(aspect_y * size)), video)) return false;
		}
		if (alpha_source < 1) {
			if (!common::push_alpha(alpha_source, video)) return false;
		}
		return true;
	}

	return filter_core(size, pos_radius, neg_radius, std::max(0.0, size * blur / 2),
		aspect_x, aspect_y, sup_ell_expo,
		move_x, move_y,
		alpha_border, alpha_source,
		method, a_param,
		color, direction,
		video);
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
