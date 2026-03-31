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
	struct common {
		using D3D = d3d_service::D3D;

		static bool init();
		static inline D3D::ComPtr<::ID3D11ComputeShader> cs_bin_pass_1 = nullptr;

		struct cs_cbuff_bin_inf_def {
			int32_t size_mid_x, size_mid_y, size_dst_x, size_dst_y;
			int32_t range_src_t, range_src_b, range_mid_l, range_mid_r;
			int32_t delta_x, delta_y;
			uint32_t size_arc_x, size_arc_y;
			uint32_t stride_mid;
			float thresh, alpha_base;

			[[maybe_unused]] uint8_t _pad[4];
		};
		static_assert(sizeof(cs_cbuff_bin_inf_def) % 16 == 0);

		struct buff_spec {
			constexpr static uint32_t elem_size_arc = 2 * sizeof(int32_t);
			static void prepare_arc(double radius_x, double radius_y, double superellipse_exp,
				double delta_x, double delta_y, uint32_t arc_width, uint32_t arc_height,
				::ID3D11UnorderedAccessView* uav_arc);
			constexpr static uint32_t elem_size_mid = 2 * sizeof(uint32_t);
			static uint32_t stride_mid(int width_src, int height_src, int width_dst, int height_dst);
			static void get_size_mid(int width_src, int height_src, int width_dst, int height_dst, uint32_t& length);
		};
	};
}
