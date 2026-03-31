/*
The MIT License (MIT)

Copyright (c) 2026 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include <string>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>


////////////////////////////////
// string conversions.
////////////////////////////////
namespace String_Service
{
	std::wstring to_wstring(std::string_view const& str, UINT code_page = CP_UTF8)
	{
		// convert to wide string using ::MultiByteToWideChar.
		int const size_needed = ::MultiByteToWideChar(code_page, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
		std::wstring result{};
		if (size_needed > 0) {
			result.resize_and_overwrite(size_needed, [&](wchar_t* buf, size_t buf_size) -> size_t {
				int ret = ::MultiByteToWideChar(code_page, 0, str.data(), static_cast<int>(str.size()), reinterpret_cast<wchar_t*>(buf), static_cast<int>(buf_size));
				return ret > 0 ? static_cast<size_t>(ret) : 0;
			});
		}
		return result;
	}
	std::string to_string(std::wstring_view const& str, UINT code_page = CP_UTF8)
	{
		// convert to narrow string using ::WideCharToMultiByte.
		int const size_needed = ::WideCharToMultiByte(code_page, 0, str.data(), static_cast<int>(str.size()), nullptr, 0, nullptr, nullptr);
		std::string result{};
		if (size_needed > 0) {
			result.resize_and_overwrite(size_needed, [&](char* buf, size_t buf_size) -> size_t {
				int ret = ::WideCharToMultiByte(code_page, 0, str.data(), static_cast<int>(str.size()), buf, static_cast<int>(buf_size), nullptr, nullptr);
				return ret > 0 ? static_cast<size_t>(ret) : 0;
			});
		}
		return result;
	}
}