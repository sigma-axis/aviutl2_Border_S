/*
The MIT License (MIT)

Copyright (c) 2026 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <vector>
#include <ranges>
#include <mutex>

#include "finalizing.hpp"
#define ANON_NS_B namespace {
#define ANON_NS_E }


////////////////////////////////
// finalization facility.
////////////////////////////////
ANON_NS_B
std::vector<void(*)()> finalizers{};
std::mutex mutex{};
void free_core(bool do_lock)
{
	decltype(finalizers) temp{};
	if (!finalizers.empty()) {
		if (do_lock) {
			std::lock_guard lk{ mutex };
			temp.swap(finalizers);
		}
		else temp.swap(finalizers);
	}
	for (auto const& free : temp | std::views::reverse)
		free();
}
ANON_NS_E

void AviUtl2::finalizing::Register(void(*free)())
{
	std::lock_guard lk{ mutex };
	finalizers.push_back(free);
}
void AviUtl2::finalizing::Free()
{
	free_core(true);
}


////////////////////////////////
// exported function for finalize.
////////////////////////////////
extern "C" __declspec(dllexport) void UninitializePlugin()
{
	free_core(false);
}
