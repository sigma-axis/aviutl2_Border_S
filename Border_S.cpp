/*
The MIT License (MIT)

Copyright (c) 2026 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <cstdint>
#include <algorithm>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <plugin2.h>

#include "logging.hpp"
#include "finalizing.hpp"

#include "Filter_Border2_S.hpp"
#include "Filter_Rounding2_S.hpp"
#include "Filter_Outline2_S.hpp"

#include "Border_S.hpp"

#define ANON_NS_B namespace {
#define ANON_NS_E }


ANON_NS_B;
////////////////////////////////
// plugin info.
////////////////////////////////
constinit COMMON_PLUGIN_TABLE plugin_table = {
	.name = PLUGIN_NAME,
	.information = PLUGIN_INFO_FMT(PLUGIN_NAME, PLUGIN_VERSION, PLUGIN_AUTHOR),
};
#define LEAST_AVIUTL2_VER_STR	"version 2.0beta39"
constexpr uint32_t least_aviutl2_ver_num = 2003900;

ANON_NS_E

////////////////////////////////
// exported functions.
////////////////////////////////

// least version (since AviUtl2 beta33).
extern "C" __declspec(dllexport) DWORD RequiredVersion()
{
	return least_aviutl2_ver_num;
}

// least version (in case of AviUtl2 before beta33).
extern "C" __declspec(dllexport) bool InitializePlugin(DWORD version)
{
	if (version >= least_aviutl2_ver_num) return true;

	AviUtl2::logging::error(L"Requires AviUtl ExEdit2 " LEAST_AVIUTL2_VER_STR L" or later!");
	::MessageBoxW(nullptr,
		PLUGIN_NAME L" は AviUtl ExEdit2 " LEAST_AVIUTL2_VER_STR L" 以降のバージョンが必要です！ "
		L"AviUtl2 の最新版を確認してください．\n"
		PLUGIN_NAME L" requires AviUtl ExEdit2 " LEAST_AVIUTL2_VER_STR L" or later! "
		L"Make sure your AviUtl2 is the latest.",
		PLUGIN_NAME, MB_OK | MB_ICONERROR);
	return false;
}

// information.
extern "C" __declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable()
{
	return &plugin_table;
}

// register.
extern "C" __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host)
{
	using namespace Border_S;

	// フィルタを登録．
	for (auto* table : {
		&Filter::Border2_S::table,
		&Filter::Rounding2_S::table,
		&Filter::Outline2_S::table,
	}) host->register_filter_plugin(table);

	// キャッシュ破棄のコールバック登録．
	host->register_clear_cache_handler([](auto) static {
		AviUtl2::finalizing::Free();
	});
}
