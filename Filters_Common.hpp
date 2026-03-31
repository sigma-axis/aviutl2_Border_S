/*
The MIT License (MIT)

Copyright (c) 2026 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include "d3d_service.hpp"
#include <filter2.h>

namespace Border_S::Filter::common
{
	struct methods {
		enum id : int {
			bin = 0,
			bin2x = 1,
			bin_smooth = 2,
			sum = 3,
			max = 4,
		};
		constexpr static id clamp(int value) { return static_cast<id>(std::min(std::max(value, 0), 4)); }
		constexpr static FILTER_ITEM_SELECT::ITEM items[] = {
			{ L"2値化", bin },
			{ L"2値化倍精度", bin2x },
			{ L"2値化スムーズ", bin_smooth },
			{ L"総和", sum },
			{ L"最大値", max },
			{ nullptr, {} },
		};
		static constexpr double second_param_a(double param_a, id method)
		{
			switch (method) {
			case bin: case bin2x: case bin_smooth: return 0.5;
			default: return param_a;
			}
		}
		static constexpr id intermed_method(id method)
		{
			switch (method) {
			case bin: case bin2x: case bin_smooth: return bin;
			default: return method;
			}
		}
	};

	// converts numbers [-3, 3] into [0, +oo], for the exponent of superellipses.
	constexpr double conv_sup_ell_expo(double track_value)
	{
		// +3.0 -> box,
		// +1.0 -> circle,
		//  0.0 -> rhombus,
		// -0.6 -> astroid,
		// -3.0 -> cross.
		return (3 + track_value) / (3 - track_value);
	}

	/**
	* @brief Multiplies the alpha values to the current object.
	* @param alpha The alpha multiplier to apply.
	* @param video Pointer to the FILTER_PROC_VIDEO structure representing the current filter state.
	* @return Returns true if function succeeds; false otherwise.
	*/
	bool push_alpha(double alpha, FILTER_PROC_VIDEO* video);
	/**
	* @brief Adds the specified size to the current object. The original image is copied to the center of the new buffer. Pixels outside of the original image are initialized to zero.
	* @param diff_size_x The horizontal size difference to add. Negative values will shrink the object.
	* @param diff_size_y The vertical size difference to add. Negative values will shrink the object.
	* @param video Pointer to the FILTER_PROC_VIDEO structure representing the current filter state.
	* @return Returns true if function succeeds; false otherwise.
	*/
	bool add_size(int diff_size_x, int diff_size_y, FILTER_PROC_VIDEO* video);

	/**
	* @brief Generates a shader resource view by applying a sequential influence/deflection process from a source shader resource view to a destination texture.
	* @param width_src Width of the source texture in pixels.
	* @param height_src Height of the source texture in pixels.
	* @param width_dst Width of the destination texture in pixels.
	* @param height_dst Height of the destination texture in pixels.
	* @param offset_x Horizontal offset of the source within the destination (in destination space).
	* @param offset_y Vertical offset of the source within the destination (in destination space).
	* @param srv_src Pointer to the source ID3D11ShaderResourceView providing the input texture.
	* @param is_src_scalar True if the source is a scalar (single-channel) texture; false otherwise.
	* @param inf_def_seq Pointer to an array of double values representing the influence/deflection sequence.
	* @param inf_def_num Number of elements in inf_def_seq.
	* @param blur Blur radius to apply.
	* @param aspect_x Horizontal aspect scaling factor.
	* @param aspect_y Vertical aspect scaling factor.
	* @param superellipse_exp Exponent used for superellipse shaping of the kernel.
	* @param method Algorithm method identifier (id) selecting the computation strategy.
	* @param a_param Additional method-specific parameter.
	* @return A D3D::ComPtr<::ID3D11ShaderResourceView> referencing the generated destination shader resource view. Returns an empty/null `ComPtr` on failure.
	*/
	D3D::ComPtr<::ID3D11ShaderResourceView> sequential_inf_def(
		int width_src, int height_src,
		int width_dst, int height_dst,
		double offset_x, double offset_y, // offset of source within dest.
		::ID3D11ShaderResourceView* srv_src, bool is_src_scalar,
		double const* inf_def_seq, int inf_def_num, double blur,
		double aspect_x, double aspect_y,
		double superellipse_exp,
		methods::id method, double a_param);

}