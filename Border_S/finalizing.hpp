/*
The MIT License (MIT)

Copyright (c) 2026 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

namespace AviUtl2::finalizing
{
	/**
	* @brief Registers a function to be called when the application is closing or intentionally releasing caches.
	* @param free A pointer to the function to be called to free resources.
	*/
	void Register(void(*free)());
	void Free();

	namespace helpers
	{
		// A helper class to manage the initialization and termination, using Register() and Free() functions.
		class init_state {
			enum class state {
				clear,
				ready,
				fail,
			} curr = state::clear;

		public:
			void init(void(*free)(), auto&& init_lambda, auto&&... args)
			{
				if (curr == state::clear) {
					curr = state::fail;
					Register(free);

					if (init_lambda(args...))
						curr = state::ready;
				}
			}

			constexpr operator bool() const { return curr == state::ready; }
			constexpr void clear() { curr = state::clear; }
		};
	}
}
