/*
The MIT License (MIT)

Copyright (c) 2026 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include <cstdint>

#include "d3d_service.hpp"

namespace Border_S::method
{
	struct max {
		using D3D = d3d_service::D3D;

		static bool inflate(
			bool deflation,
			int width_src, int height_src,
			int width_dst, int height_dst,
			int offset_x, int offset_y, double delta_x, double delta_y,
			::ID3D11ShaderResourceView* srv_src, ::ID3D11UnorderedAccessView* uav_shape,
			double radius_x, double radius_y, double superellipse_exp,
			D3D::cs_views const& arc, D3D::cs_views const& mid);
		struct buff_spec {
			constexpr static uint32_t elem_size_arc = 2 * sizeof(int32_t);
			static void get_size_arc(double radius_x, double radius_y, uint32_t& length);
			constexpr static ::DXGI_FORMAT format_mid = ::DXGI_FORMAT_R32G32_FLOAT;
			constexpr static int dimension_mid = 2;
			static void get_size_mid(double radius_x, double radius_y, double superellipse_exp,
				int width_src, int height_src, int width_dst, int height_dst,
				uint32_t& width, uint32_t& height);
		};
	};
}
