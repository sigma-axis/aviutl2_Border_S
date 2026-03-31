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
#include "Filter_Rounding2_S.hpp"

#include "Border_S.hpp"

#define ANON_NS_B namespace {
#define ANON_NS_E }


ANON_NS_B
////////////////////////////////
// setting parameters.
////////////////////////////////
namespace params
{
	FILTER_ITEM_TRACK radius{ L"半径", 32.00, 0.00, 500.00, 0.01 };
	FILTER_ITEM_TRACK blur{ L"ぼかし", 0.00, 0.00, 500.00, 0.01 };
	using methods = common::methods;
	FILTER_ITEM_SELECT method{ L"方式", methods::bin_smooth, const_cast<FILTER_ITEM_SELECT::ITEM*>(methods::items) };
	FILTER_ITEM_TRACK a_param{ L"α調整", 50.00, 0.00, 100.00, 0.01 };

	FILTER_ITEM_GROUP group_shrink{ L"縁の縮小の設定", false };
	FILTER_ITEM_TRACK shrink{ L"縁の縮小", 0.00, 0.00, 500.00, 0.01 };
	FILTER_ITEM_CHECK fixed_size{ L"サイズ固定", true };

	FILTER_ITEM_GROUP group_others{ L"その他", false };
	FILTER_ITEM_TRACK alpha{ L"透明度", +100.00, -100.00, +100.00, 0.01 };
	FILTER_ITEM_TRACK aspect{ L"縦横比", 0.000, -100.000, +100.000, 0.001 };
	FILTER_ITEM_TRACK sup_ell_expo{ L"膨らみ", 100.000, -300.000, +300.00, 0.001 };

	constexpr void* all[] = {
		&radius,
		&blur,
		&method,
		&a_param,

		&group_shrink,
		&shrink,
		&fixed_size,

		&group_others,
		&alpha,
		&aspect,
		&sup_ell_expo,

		nullptr,
	};
}

////////////////////////////////
// filter functions.
////////////////////////////////
bool filter_core(
	double radius, double shrink, double blur,
	double aspect_x, double aspect_y, double superellipse_exp,
	double alpha, bool fixed_size,
	params::methods::id method, double a_param,
	FILTER_PROC_VIDEO* video)
{
	// determine the input and output dimensions.
	int const width_src = video->object->width, height_src = video->object->height;

	// determine inflation/deflation sequence.
	int inf_def_num = 0;
	double inf_def_seq[2];
	{
		auto const append_inf_def = [&](double r) -> void {
			if (r == 0) return;
			else if (inf_def_num > 0 &&
				(r > 0) == (inf_def_seq[inf_def_num - 1] > 0))
				inf_def_seq[inf_def_num - 1] += r;
			else inf_def_seq[inf_def_num++] = r;
		};
		double const
			r = std::max(0.0, radius - blur),
			s = std::max(0.0, shrink + blur);
		append_inf_def(-(r + s));
		append_inf_def(r);
	}

	// image operations.
	auto obj = video->get_image_texture2d();
	if (!D3D::init(obj)) return false;

	// prepare views.
	auto srv_obj = D3D::to_shader_resource_view(obj);
	if (srv_obj == nullptr) return false;
	auto uav_obj = D3D::to_unordered_access_view(obj);
	if (uav_obj == nullptr) return false;

	// apply the sequence of inflation/deflation.
	auto srv_shape = common::sequential_inf_def(
		width_src, height_src, width_src, height_src, 0, 0,
		srv_obj.Get(), false,
		inf_def_seq, inf_def_num, blur,
		aspect_x, aspect_y, superellipse_exp,
		method, a_param);
	if (srv_shape == nullptr) return false;

	// combine with the original image.
	if (!image_ops::carve(
		width_src, height_src,
		width_src, height_src,
		0, 0, srv_shape.Get(), uav_obj.Get(),
		alpha, false)) return false;

	// reduce the size if specified.
	if (!fixed_size) {
		if (!common::add_size(
			static_cast<int>(std::ceil(-aspect_x * shrink)),
			static_cast<int>(std::ceil(-aspect_y * shrink)), video)) return false;
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
		radius = std::min(std::max(params::radius.value, params::radius.s), params::radius.e),
		blur = std::min(std::max(params::blur.value, params::blur.s), params::blur.e),
		a_param = std::min(std::max(params::a_param.value, params::a_param.s), params::a_param.e) / 100,

		shrink = std::min(std::max(params::shrink.value, params::shrink.s), params::shrink.e),
		alpha = std::min(std::max(params::alpha.value, params::alpha.s), params::alpha.e) / 100,

		aspect = std::min(std::max(params::aspect.value, params::aspect.s), params::aspect.e) / 100,
		sup_ell_expo = common::conv_sup_ell_expo(
			std::min(std::max(params::sup_ell_expo.value, params::sup_ell_expo.s), params::sup_ell_expo.e) / 100);

	auto const method = params::methods::clamp(params::method.value);
	auto const fixed_size = alpha < 1 || params::fixed_size.value;

	// further calculations.
	double const
		aspect_x = std::min(1.0, 1 - aspect),
		aspect_y = std::min(1.0, 1 + aspect);

	// handle trivial cases.
	if (alpha == 0) return true;
	else if (radius <= 0 && blur <= 0 && shrink <= 0) return true;

	return filter_core(
		radius, shrink, blur / 2,
		aspect_x, aspect_y, sup_ell_expo,
		alpha, fixed_size,
		method, a_param,
		video);
}
ANON_NS_E


////////////////////////////////
// exports.
////////////////////////////////
#define FILTER_NAME	L"角丸めσ"
constinit FILTER_PLUGIN_TABLE Border_S::Filter::Rounding2_S::table{
	.flag = FILTER_PLUGIN_TABLE::FLAG_VIDEO,
	.name = FILTER_NAME,
	.label = FILTER_LABEL_FMT(L"クリッピング"),
	.information = PLUGIN_INFO_FMT(FILTER_NAME, PLUGIN_VERSION, PLUGIN_AUTHOR),
	.items = const_cast<void**>(params::all),
	.func_proc_video = &filter,
};
