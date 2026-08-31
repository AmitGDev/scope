#ifndef AMITG_FC_SCOPE_HPP_
#define AMITG_FC_SCOPE_HPP_

/*
    scope.hpp
    Copyright (c) 2026, Amit Gefen

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
    deal in the Software without restriction, including without limitation the
    rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#include <type_traits>
#include <utility>

// Named to match std::experimental::scope_exit from the Library Fundamentals
// TS.
template <typename F>
class scope_exit final {  // NOLINT(readability-identifier-naming)
 public:
  // The constructor moves the callable into function_, so its move construction
  // must be non-throwing to uphold the constructor's noexcept guarantee.
  static_assert(std::is_nothrow_move_constructible_v<F>);

  // The stored callable must not throw when invoked by the destructor.
  static_assert(std::is_nothrow_invocable_v<F&>);

  explicit scope_exit(F&& function) noexcept : function_(std::move(function)) {}

  // Copy is deleted because two guards must never own (and therefore run)
  // the same cleanup. Move is also deleted because transferring ownership
  // would require the source guard to become inactive. This guard has no
  // inactive state and is therefore pinned to the scope in which it is created.
  scope_exit(const scope_exit&) = delete;
  scope_exit& operator=(const scope_exit&) = delete;
  scope_exit(scope_exit&&) = delete;
  scope_exit& operator=(scope_exit&&) = delete;

  ~scope_exit() noexcept { function_(); }

 private:
  F function_;
};

#endif  // AMITG_FC_SCOPE_HPP_