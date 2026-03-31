/*
The MIT License (MIT)

Copyright (c) 2026 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <cstdint>
#include <string>

#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler")

#include "logging.hpp"
namespace logging = AviUtl2::logging;
#if _DEBUG
#include "string_service.hpp"
#endif
#include "finalizing.hpp"

#include "d3d_service.hpp"
using D3D = d3d_service::D3D;
template<class T> using ComPtr = D3D::ComPtr<T>;

#define ANON_NS_B namespace {
#define ANON_NS_E }


////////////////////////////////
// Resource managements.
////////////////////////////////
ANON_NS_B;
constinit AviUtl2::finalizing::helpers::init_state init_state{};
void quit()
{
	D3D::device.Reset();
	D3D::cxt.Reset();

	init_state.clear();
}
ANON_NS_E;
bool D3D::init(::ID3D11DeviceChild* src)
{
	init_state.init(&quit, [src] -> bool {
		return

			(src->GetDevice(&device), device != nullptr) &&
			(device->GetImmediateContext(&cxt), cxt != nullptr) &&

			true;
	});
	return init_state;
}


////////////////////////////////
// D3D implementations.
////////////////////////////////
ComPtr<::ID3D11ComputeShader> D3D::create_compute_shader(char const* src, size_t size, char const* name)
{
	ComPtr<::ID3D11ComputeShader> ret;
	ComPtr<::ID3DBlob> blob, errors;
	if (S_OK != ::D3DCompile(src, size, name,
		nullptr, nullptr, "csmain", "cs_5_0",
		D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0,
		&blob, &errors)) {
		DBG_ERROR(String_Service::to_wstring({
			reinterpret_cast<char const*>(errors->GetBufferPointer()),
			errors->GetBufferSize(),
		}).c_str());
		return nullptr;
	}
	DBG_INFO((std::wstring{ L"Compiled compute shader: " } + String_Service::to_wstring(name)
		+ L"; source size = " + std::to_wstring(size)
		+ L", blob size = " + std::to_wstring(blob->GetBufferSize())).c_str());
	if (S_OK != device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &ret)) {
		logging::error(L"Failed to create compute shader!");
		return nullptr;
	}
	return ret;
}

ComPtr<::ID3D11SamplerState> D3D::create_sampler_state(D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE address_u, D3D11_TEXTURE_ADDRESS_MODE address_v)
{
	::D3D11_SAMPLER_DESC desc{
		.Filter = filter,
		.AddressU = address_u,
		.AddressV = address_v,
		.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
		.MipLODBias = 0.0f,
		.MaxAnisotropy = 1,
		.ComparisonFunc = D3D11_COMPARISON_NEVER,
		.BorderColor = { 1.0f, 1.0f, 1.0f, 1.0f },
		.MinLOD = -FLT_MAX,
		.MaxLOD = FLT_MAX,
	};
	ComPtr<::ID3D11SamplerState> ret;
	if (S_OK != device->CreateSamplerState(&desc, &ret)) {
		logging::error(L"Failed to create sampler state!");
		return nullptr;
	}
	return ret;
}


ComPtr<::ID3D11Buffer> D3D::create_const_buffer(void const* data, size_t size)
{
	::D3D11_BUFFER_DESC desc{
		.ByteWidth = static_cast<uint32_t>(size),
		.Usage = ::D3D11_USAGE_DEFAULT,
		.BindFlags = ::D3D11_BIND_CONSTANT_BUFFER,
		.CPUAccessFlags = 0,
		.MiscFlags = 0,
		.StructureByteStride = 0,
	};
	::D3D11_SUBRESOURCE_DATA init_data{
		.pSysMem = data,
		.SysMemPitch = 0,
		.SysMemSlicePitch = 0,
	};
	ComPtr<::ID3D11Buffer> ret;
	if (S_OK != device->CreateBuffer(&desc, &init_data, &ret)) {
		logging::error(L"Failed to create const buffer!");
		return nullptr;
	}
	return ret;
}

ComPtr<::ID3D11Texture1D> D3D::create_texture(::DXGI_FORMAT format, uint32_t size, void const* init)
{
	::D3D11_TEXTURE1D_DESC desc{
		.Width = size,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = format,
		.Usage = ::D3D11_USAGE_DEFAULT,
		.BindFlags = ::D3D11_BIND_SHADER_RESOURCE | ::D3D11_BIND_RENDER_TARGET | ::D3D11_BIND_UNORDERED_ACCESS,
		.CPUAccessFlags = 0,
		.MiscFlags = 0,
	};
	::D3D11_SUBRESOURCE_DATA init_data{
		.pSysMem = init,
		.SysMemPitch = 0,
		.SysMemSlicePitch = 0,
	};
	ComPtr<::ID3D11Texture1D> ret;
	if (S_OK != device->CreateTexture1D(&desc, init != nullptr ? &init_data : nullptr, &ret)) {
		logging::error(L"Failed to create 1D texture!");
		return nullptr;
	}
	return ret;
}

ComPtr<::ID3D11Texture2D> D3D::create_texture(::DXGI_FORMAT format, uint32_t width, uint32_t height, void const* init)
{
	::D3D11_TEXTURE2D_DESC desc{
		.Width = width,
		.Height = height,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = format,
		.SampleDesc = {.Count = 1, .Quality = 0 },
		.Usage = ::D3D11_USAGE_DEFAULT,
		.BindFlags = ::D3D11_BIND_SHADER_RESOURCE | ::D3D11_BIND_RENDER_TARGET | ::D3D11_BIND_UNORDERED_ACCESS,
		.CPUAccessFlags = 0,
		.MiscFlags = 0,
	};
	::D3D11_SUBRESOURCE_DATA init_data{
		.pSysMem = init,
		.SysMemPitch = 0,
		.SysMemSlicePitch = 0,
	};
	ComPtr<::ID3D11Texture2D> ret;
	if (S_OK != device->CreateTexture2D(&desc, init != nullptr ? &init_data : nullptr, &ret)) {
		logging::error(L"Failed to create 2D texture!");
		return nullptr;
	}
	return ret;
}

ComPtr<::ID3D11Buffer> D3D::create_structured_buffer(uint32_t size_element, uint32_t size_byte, void const* init)
{
	::D3D11_BUFFER_DESC desc{
		.ByteWidth = size_byte,
		.Usage = ::D3D11_USAGE_DEFAULT,
		.BindFlags = ::D3D11_BIND_SHADER_RESOURCE | ::D3D11_BIND_UNORDERED_ACCESS,
		.CPUAccessFlags = 0,
		.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
		.StructureByteStride = size_element,
	};
	::D3D11_SUBRESOURCE_DATA init_data{
		.pSysMem = init,
		.SysMemPitch = 0,
		.SysMemSlicePitch = 0,
	};
	ComPtr<::ID3D11Buffer> ret;
	if (S_OK != device->CreateBuffer(&desc, init != nullptr ? &init_data : nullptr, &ret)) {
		logging::error(L"Failed to create structured buffer!");
		return nullptr;
	}
	return ret;
}

ComPtr<::ID3D11Texture2D> D3D::clone(::ID3D11Texture2D* src, bool copy)
{
	::D3D11_TEXTURE2D_DESC desc;
	src->GetDesc(&desc);
	ComPtr<::ID3D11Texture2D> ret;
	if (S_OK != device->CreateTexture2D(&desc, nullptr, &ret)) {
		logging::error(L"Failed to create 2D texture!");
		return nullptr;
	}
	if (copy) cxt->CopyResource(ret.Get(), src);
	return ret;
}

ComPtr<::ID3D11ShaderResourceView> D3D::to_shader_resource_view(::ID3D11Resource* resource)
{
	ComPtr<::ID3D11ShaderResourceView> ret;
	if (S_OK != device->CreateShaderResourceView(resource, nullptr, &ret)) {
		logging::error(L"Failed to create shader resource view!");
		return nullptr;
	}
	return ret;
}
ComPtr<::ID3D11ShaderResourceView> D3D::to_shader_resource_view(::ID3D11View* view)
{
	auto resource = get_resource(view);
	if (resource == nullptr) {
		logging::error(L"Failed to get resource from view!");
		return nullptr;
	}
	return to_shader_resource_view(resource.Get());
}

ComPtr<::ID3D11UnorderedAccessView> D3D::to_unordered_access_view(::ID3D11Resource* resource)
{
	ComPtr<::ID3D11UnorderedAccessView> ret;
	if (S_OK != device->CreateUnorderedAccessView(resource, nullptr, &ret)) {
		logging::error(L"Failed to create unordered access view!");
		return nullptr;
	}
	return ret;
}

ComPtr<::ID3D11UnorderedAccessView> D3D::to_unordered_access_view(::ID3D11View* view)
{
	auto resource = get_resource(view);
	if (resource == nullptr) {
		logging::error(L"Failed to get resource from view!");
		return nullptr;
	}
	return to_unordered_access_view(resource.Get());
}

ComPtr<::ID3D11RenderTargetView> D3D::to_render_target_view(::ID3D11Resource* resource)
{
	ComPtr<::ID3D11RenderTargetView> ret;
	if (S_OK != device->CreateRenderTargetView(resource, nullptr, &ret)) {
		logging::error(L"Failed to create render target view!");
		return nullptr;
	}
	return ret;
}

ComPtr<::ID3D11RenderTargetView> D3D::to_render_target_view(::ID3D11View* view)
{
	auto resource = get_resource(view);
	if (resource == nullptr) {
		logging::error(L"Failed to get resource from view!");
		return nullptr;
	}
	return to_render_target_view(resource.Get());
}

ComPtr<::ID3D11Resource> D3D::get_resource(::ID3D11View* view)
{
	ComPtr<::ID3D11Resource> resource;
	view->GetResource(&resource);
	return resource;
}
