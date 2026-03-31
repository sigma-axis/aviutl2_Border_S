/*
The MIT License (MIT)

Copyright (c) 2026 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d11.h>
#pragma comment(lib, "d3d11")

#include <wrl/client.h>

////////////////////////////////
// wrapping class for D3D11.
////////////////////////////////
namespace d3d_service
{
	// Wrapper for D3D11 device and related resources. The main purpose is to simplify resource creation and management, and to provide some utility functions for the methods.
	struct D3D {
		template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
		static bool init(::ID3D11DeviceChild* src);
		static inline ComPtr<::ID3D11Device> device{};
		static inline ComPtr<::ID3D11DeviceContext> cxt{};

		static ComPtr<::ID3D11ComputeShader> create_compute_shader(char const* src, size_t size, char const* name = nullptr);
		template<size_t N>
		static auto create_compute_shader(char const(&src)[N], char const* name = nullptr)
		{
			return create_compute_shader(src, N, name);
		}

		static ComPtr<::ID3D11SamplerState> create_sampler_state(D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE address_u, D3D11_TEXTURE_ADDRESS_MODE address_v);
		static auto create_sampler_state(D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE address)
		{
			return create_sampler_state(filter, address, address);
		}

		static ComPtr<::ID3D11Buffer> create_const_buffer(void const* data, size_t size);
		template<class T>
		static auto create_const_buffer(T const& data)
		{
			using the_type = std::decay_t<T>;
			static_assert(sizeof(the_type) % 16 == 0);
			return create_const_buffer(&data, sizeof(the_type));
		}

		static ComPtr<::ID3D11Texture1D> create_texture(::DXGI_FORMAT format, uint32_t size, void const* init = nullptr);
		static ComPtr<::ID3D11Texture2D> create_texture(::DXGI_FORMAT format, uint32_t width, uint32_t height, void const* init = nullptr);
		static auto create_texture(::DXGI_FORMAT format, int32_t size, void const* init = nullptr)
		{
			return create_texture(format, static_cast<uint32_t>(size), init);
		}
		static auto create_texture(::DXGI_FORMAT format, int32_t width, int32_t height, void const* init = nullptr)
		{
			return create_texture(format, static_cast<uint32_t>(width), static_cast<uint32_t>(height), init);
		}

		static ComPtr<::ID3D11Buffer> create_structured_buffer(uint32_t size_element, uint32_t size_byte, void const* init = nullptr);
		template<class T>
		static auto create_structured_buffer(uint32_t size_byte, T const* init)
		{
			return create_structured_buffer(static_cast<uint32_t>(sizeof(T)), size_byte, init);
		}

		static ComPtr<::ID3D11Texture2D> clone(::ID3D11Texture2D* src, bool copy = true);

		static ComPtr<::ID3D11ShaderResourceView> to_shader_resource_view(::ID3D11Resource* resource);
		static ComPtr<::ID3D11ShaderResourceView> to_shader_resource_view(::ID3D11View* view);
		static ComPtr<::ID3D11UnorderedAccessView> to_unordered_access_view(::ID3D11Resource* resource);
		static ComPtr<::ID3D11UnorderedAccessView> to_unordered_access_view(::ID3D11View* view);
		static ComPtr<::ID3D11RenderTargetView> to_render_target_view(::ID3D11Resource* resource);
		static ComPtr<::ID3D11RenderTargetView> to_render_target_view(::ID3D11View* view);
		static ComPtr< ::ID3D11Resource> get_resource(::ID3D11View* view);
		template<class T>
		static ComPtr<T> get_resource(::ID3D11View* view)
		{
			ComPtr<T> ret;
			view->GetResource(reinterpret_cast<::ID3D11Resource**>(ret.ReleaseAndGetAddressOf()));
			return ret;
		}

		constexpr static uint32_t max_image_size = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
		constexpr static uint32_t buffer_stride_align = 1 << 8;
		constexpr static float max_f16_lt_1 = 1.0f - 1.0f / (1 << 11); // maximum float16 < 1.
		constexpr static float min_f16_gt_0 = 1.0f / (1 << 24); // minimum float16 > 0. note that float16 preserve denorms (https://learn.microsoft.com/en-us/windows/win32/direct3d11/floating-point-rules).
		constexpr static float zero_color[4] = { 0, 0, 0, 0 };
		constexpr static uint32_t zero_color_i[4] = { 0, 0, 0, 0 };

		constexpr static void clamp_extension_2d(int& ext, int size_src)
		{
			const auto l = size_src + 2 * ext;
			if (l <= 0) {
				const auto d = (2 - l) >> 1;
				ext += d;
			}
			else if (l > max_image_size) {
				const auto d = (l - max_image_size + 1) >> 1;
				ext -= d;
			}
		}
		constexpr static void clamp_extension_2d(int& ext_neg, int& ext_pos, int size_src)
		{
			const auto l = ext_neg + size_src + ext_pos;
			if (l <= 0) {
				const auto d = (2 - l) >> 1;
				ext_neg += d; ext_pos += d;
			}
			else if (l > max_image_size) {
				const auto d = (l - max_image_size + 1) >> 1;
				ext_neg -= d; ext_pos -= d;
			}
		}

		struct cs_views {
			::ID3D11ShaderResourceView* srv;
			::ID3D11UnorderedAccessView* uav;
		};
	};
}
