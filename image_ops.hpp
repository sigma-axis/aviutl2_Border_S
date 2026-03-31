/*
The MIT License (MIT)

Copyright (c) 2026 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include "d3d_service.hpp"

namespace Border_S::image_ops
{
	// A structure representing a color with floating-point components for red, green, blue, and alpha channels.
	struct color_float {
		float r = 0, g = 0, b = 0, a = 0;
		constexpr color_float() = default;
		constexpr color_float(float r, float g, float b, float a = 1.0f) : r{ r }, g{ g }, b{ b }, a{ a } {}
		// assume coded as 0xRRGGBB. highest 8 bits are ignored.
		constexpr static color_float from_rgb(uint32_t color)
		{
			return {
				(color & 0xff0000) / (1.0f * 0xff0000),
				(color & 0x00ff00) / (1.0f * 0x00ff00),
				(color & 0x0000ff) / (1.0f * 0x0000ff),
			};
		}
		constexpr static color_float from_argb(float r, float g, float b, float a = 1.0f)
		{
			return { a * r, a * g, a * b, a };
		}
		// assume coded as 0xAARRGGBB.
		constexpr static color_float from_argb(uint32_t color)
		{
			return from_argb(
				(color & 0x00ff0000) / (1.0f * 0x00ff0000),
				(color & 0x0000ff00) / (1.0f * 0x0000ff00),
				(color & 0x000000ff) / (1.0f * 0x000000ff),
				(color & 0xff000000) / (1.0f * 0xff000000)
			);
		}
		// assume coded as 0xAARRGGBB.
		constexpr static color_float from_pargb(uint32_t color)
		{
			return {
				(color & 0x00ff0000) / (1.0f * 0x00ff0000),
				(color & 0x0000ff00) / (1.0f * 0x0000ff00),
				(color & 0x000000ff) / (1.0f * 0x000000ff),
				(color & 0xff000000) / (1.0f * 0xff000000),
			};
		}
	};
	struct ops {
		/**
		* @brief Extracts the alpha channel from a region of a source shader resource view and writes it into a destination unordered access view.
		* @param width Width of the region to extract, in pixels.
		* @param height Height of the region to extract, in pixels.
		* @param src_left Left (x) coordinate of the region in the source resource, in pixels.
		* @param src_top Top (y) coordinate of the region in the source resource, in pixels.
		* @param src Pointer to the source ID3D11ShaderResourceView containing the input image (must be a valid view).
		* @param dst_left Left (x) coordinate of the region in the destination resource where alpha values will be written, in pixels.
		* @param dst_top Top (y) coordinate of the region in the destination resource where alpha values will be written, in pixels.
		* @param dst Pointer to the destination ID3D11UnorderedAccessView that will receive the extracted alpha data (must be a valid view).
		* @returns true if the extraction and write operation succeeded; false otherwise.
		*/
		static bool extract_alpha(
			int width, int height,
			int src_left, int src_top, ::ID3D11ShaderResourceView* src,
			int dst_left, int dst_top, ::ID3D11UnorderedAccessView* dst);
		/**
		* @brief Draws an image and a shape onto a destination texture with specified color and alpha values.
		* @param width_src Width of the source image texture in pixels.
		* @param height_src Height of the source image texture in pixels.
		* @param width_dst Width of the destination texture in pixels.
		* @param height_dst Height of the destination texture in pixels.
		* @param offset_x Horizontal offset of the source within the destination (in destination space).
		* @param offset_y Vertical offset of the source within the destination (in destination space).
		* @param srv_src Pointer to the source ID3D11ShaderResourceView providing the image texture.
		* @param srv_shape Pointer to the source ID3D11ShaderResourceView providing the shape texture.
				 Must have the same width and height as the uav_dst.
		* @param uav_dst Pointer to the destination ID3D11UnorderedAccessView that will receive the drawn image.
		* @param color Color to apply to the shape.
		* @param alpha_front Alpha value for the front source image.
		* @param alpha_back Alpha value for the back source shape.
		* @returns true if the draw operation succeeded; false otherwise.
		*/
		static bool draw(
			int width_src, int height_src,
			int width_dst, int height_dst,
			int offset_x, int offset_y, // offset of source within dest.
			::ID3D11ShaderResourceView* srv_src,
			::ID3D11ShaderResourceView* srv_shape, ::ID3D11UnorderedAccessView* uav_dst,
			color_float const& color,
			double alpha_front, double alpha_back);
		/**
		* @brief Fills a region of an image with specified color and alpha values.
		* @param width_shape Width of the source shape texture in pixels, specifying the region to fill.
		* @param height_shape Height of the source shape texture in pixels, specifying the region to fill.
		* @param width_dst Width of the destination texture in pixels.
		* @param height_dst Height of the destination texture in pixels.
		* @param offset_x Horizontal offset of the shape within the destination (in destination space).
		* @param offset_y Vertical offset of the shape within the destination (in destination space).
		* @param srv_shape Pointer to the source ID3D11ShaderResourceView providing the shape texture.
		* @param uav_dst Pointer to the destination ID3D11UnorderedAccessView that will provide the source image and receive the recolored image.
		* @param color Color to apply to the shape.
		* @param invert True to invert the shape's colors; false otherwise.
		* @param alpha_front Alpha value for the front source shape.
		* @param alpha_back Alpha value for the back source image.
		* @returns true if the function succeeded; false otherwise.
		*/
		static bool recolor(
			int width_shape, int height_shape,
			int width_dst, int height_dst,
			int offset_x, int offset_y, // offset of shape within dest.
			::ID3D11ShaderResourceView* srv_shape, ::ID3D11UnorderedAccessView* uav_dst,
			color_float const& color, bool invert,
			double alpha_front, double alpha_back);
		/**
		* @brief Carves a shape from a destination texture based on a source shape texture, modifying the alpha values of the destination according to the shape and specified alpha parameters.
		* @param width_shape Width of the source shape texture in pixels.
		* @param height_shape Height of the source shape texture in pixels.
		* @param width_dst Width of the destination texture in pixels.
		* @param height_dst Height of the destination texture in pixels.
		* @param offset_x Horizontal offset of the shape within the destination (in destination space).
		* @param offset_y Vertical offset of the shape within the destination (in destination space).
		* @param srv_shape Pointer to the source ID3D11ShaderResourceView providing the shape texture.
		* @param uav_dst Pointer to the destination ID3D11UnorderedAccessView that will receive the carved image.
		* @param alpha Intensity of the effect. A value of 0 means no change, +1 means simply masking by the shape, and -1 means inverting the shape's mask. Values between -1 and +1 will interpolate accordingly.
		* @param is_dst_scalar True if the destination is a scalar texture; false if half4 format.
		* @returns true if the function succeeded; false otherwise.
		*/
		static bool carve(
			int width_shape, int height_shape,
			int width_dst, int height_dst,
			int offset_x, int offset_y, // offset of shape within dest.
			::ID3D11ShaderResourceView* srv_shape, ::ID3D11UnorderedAccessView* uav_dst,
			double alpha, bool is_dst_scalar);
		/**
		* @brief Combines a source image and a shape into a destination texture with specified color and alpha values, allowing for control over the layering of the source and shape.
		* @param width_src Width of the source image in pixels.
		* @param height_src Height of the source image in pixels.
		* @param width_shape Width of the source shape texture in pixels.
		* @param height_shape Height of the source shape texture in pixels.
		* @param width_dst Width of the destination texture in pixels.
		* @param height_dst Height of the destination texture in pixels.
		* @param offset_src_x Horizontal offset of the source image within the destination (in destination space).
		* @param offset_src_y Vertical offset of the source image within the destination (in destination space).
		* @param offset_shape_x Horizontal offset of the shape within the destination (in destination space).
		* @param offset_shape_y Vertical offset of the shape within the destination (in destination space).
		* @param srv_src Pointer to the source ID3D11ShaderResourceView providing the source image.
		* @param srv_shape Pointer to the source ID3D11ShaderResourceView providing the shape texture.
		* @param uav_dst Pointer to the destination ID3D11UnorderedAccessView that will receive the combined image.
		* @param color Color to apply to the shape.
		* @param alpha_src Alpha value for the source image.
		* @param alpha_shape Alpha value for the shape.
		* @param is_src_front True if the source image should be in front of the shape; false otherwise.
		* @returns true if the function succeeded; false otherwise.
		*/
		static bool combine(
			int width_src, int height_src,
			int width_shape, int height_shape,
			int width_dst, int height_dst,
			int offset_src_x, int offset_src_y, // offset of source within dest.
			int offset_shape_x, int offset_shape_y, // offset of shape within dest.
			::ID3D11ShaderResourceView* srv_src, ::ID3D11ShaderResourceView* srv_shape,
			::ID3D11UnorderedAccessView* uav_dst,
			color_float const& color,
			double alpha_src, double alpha_shape, bool is_src_front);
		/**
		* @brief Applies a blur effect to a source image, storing the intermediate result in a temporary buffer.
		* @param width_src Width of the source image in pixels.
		* @param height_src Height of the source image in pixels.
		* @param src Source views for the image.
		* @param tmp Temporary views for intermediate results.
		* @param blur_half_x Horizontal blur radius. The half of the support of the blur kernel.
		* @param blur_half_y Vertical blur radius. The half of the support of the blur kernel.
		* @returns true if the function succeeded; false otherwise.
		*/
		static bool blur(
			int width_src, int height_src,
			D3D::cs_views const& src, D3D::cs_views const& tmp,
			double blur_half_x, double blur_half_y);
		/**
		* @brief Applies a small movement of the source image to the destination texture based on specified delta values, which is meant to be within the range [-0.5, +0.5].
		* @param width Width of the source and destination textures in pixels.
		* @param height Height of the source and destination textures in pixels.
		* @param srv_src Pointer to the source ID3D11ShaderResourceView providing the source image.
		* @param uav_dst Pointer to the destination ID3D11UnorderedAccessView that will receive the moved image.
		* @param delta_x Horizontal movement delta.
		* @param delta_y Vertical movement delta.
		* @returns true if the function succeeded; false otherwise.
		*/
		static bool delta_move(
			int width, int height,
			::ID3D11ShaderResourceView* srv_src, ::ID3D11UnorderedAccessView* uav_dst,
			double delta_x, double delta_y);

		/**
		* @brief Multiplies a uniform alpha value into a destination texture.
		* @param width Width of the destination texture in pixels.
		* @param height Height of the destination texture in pixels.
		* @param uav_dst Pointer to the destination ID3D11UnorderedAccessView that will receive the alpha value.
		* @param alpha Alpha value to multiply into the destination texture.
		* @returns true if the function succeeded; false otherwise.
		*/
		static bool push_alpha(
			int width, int height,
			::ID3D11UnorderedAccessView* uav_dst,
			double alpha)
		{
			if (alpha <= 0) {
				D3D::cxt->ClearUnorderedAccessViewFloat(uav_dst, D3D::zero_color);
				return true;
			}
			return recolor(0, 0, width, height, 0, 0,
				nullptr, uav_dst, {}, false, 0, alpha);
		}
	};
}
