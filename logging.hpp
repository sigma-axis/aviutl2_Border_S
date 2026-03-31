/*
The MIT License (MIT)

Copyright (c) 2026 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

using LPCWSTR = wchar_t const*;
#include <logger2.h>

namespace AviUtl2::logging
{
	inline LOG_HANDLE* logger = nullptr;
	inline void log(wchar_t const* mes) { logger->log(logger, mes); }
	inline void info(wchar_t const* mes) { logger->info(logger, mes); }
	inline void warn(wchar_t const* mes) { logger->warn(logger, mes); }
	inline void error(wchar_t const* mes) { logger->error(logger, mes); }
	inline void verbose(wchar_t const* mes) { logger->verbose(logger, mes); }
}

#if _DEBUG
#define DBG_LOG(mes)		(::AviUtl2::logging::log(mes))
#define DBG_INFO(mes)		(::AviUtl2::logging::info(mes))
#define DBG_WARN(mes)		(::AviUtl2::logging::warn(mes))
#define DBG_ERROR(mes)		(::AviUtl2::logging::error(mes))
#define DBG_VERBOSE(mes)	(::AviUtl2::logging::verbose(mes))
#else
#define DBG_LOG(mes)		((void)0)
#define DBG_INFO(mes)		((void)0)
#define DBG_WARN(mes)		((void)0)
#define DBG_ERROR(mes)		((void)0)
#define DBG_VERBOSE(mes)	((void)0)
#endif
