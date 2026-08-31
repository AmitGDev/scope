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

// A constructor whose noexcept-ness depends on F's move/copy constructor,
// paired with a function-try-block that invokes the source callable on
// construction failure, hits two unrelated compiler false positives:
//  - GCC/Clang can't prove the catch's rethrow is unreachable even when the
//    trait guarantees the paired try region can't throw, and warn
//    -Wterminate.
//  - MSVC's own noexcept-body checker disagrees with its own (correctly
//    computed) std::is_nothrow_move/copy_constructible_v result for
//    implicitly-generated closure special members, and warns C4297 - see
//    https://quuxplusone.github.io/blog/2023/04/17/noexcept-false-equals-default/
// Neither reflects an actual defect in the wrapped code; both are suppressed
// here rather than worked around, since no code shape has been found that
// avoids either diagnostic on its respective compiler.
#ifdef _MSC_VER
#define SCOPE_EXIT_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_BEGIN \
  __pragma(warning(push)) __pragma(warning(disable : 4297))
#define SCOPE_EXIT_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_END __pragma(warning(pop))
#elif defined(__GNUC__) || defined(__clang__)
#define SCOPE_EXIT_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_BEGIN \
  _Pragma("GCC diagnostic push")                          \
      _Pragma("GCC diagnostic ignored \"-Wterminate\"")
#define SCOPE_EXIT_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_END \
  _Pragma("GCC diagnostic pop")
#else
#define SCOPE_EXIT_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_BEGIN
#define SCOPE_EXIT_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_END
#endif

// Named to match std::experimental::scope_exit from the Library Fundamentals
// TS.
template <typename F>
class scope_exit final {  // NOLINT(readability-identifier-naming)
 public:
  // The stored callable must not throw when invoked by the destructor.
  // This is an unconditional class invariant: every instance is destroyed
  // exactly once, and the destructor's own noexcept guarantee depends on
  // this holding regardless of which constructor built the object.
  static_assert(std::is_nothrow_invocable_v<F&>);

  // Copying would give two guards ownership of the same cleanup action,
  // causing it to run more than once. Copying is therefore disabled.
  scope_exit(const scope_exit&) = delete;
  scope_exit& operator=(const scope_exit&) = delete;

  SCOPE_EXIT_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_BEGIN

  // The catch block below invokes the callable through a const reference on
  // copy failure, so it must also be invocable in that const-qualified
  // form; this is a precondition of this constructor only, not of the
  // class as a whole (a mutable-lambda F can still be used via the F&&
  // overload below).
  static_assert(std::is_nothrow_invocable_v<const F&>);

  // Allows construction from an lvalue callable by copying it into the guard.
  explicit scope_exit(const F& function) noexcept(
      std::is_nothrow_copy_constructible_v<F>) try
      : function_(function) {
  } catch (...) {
    function();
    throw;
  }

  // If constructing function_ throws, invoke the source callable before
  // propagating so the resource it's responsible for can still be cleaned
  // up. This is best-effort: function's resulting state depends on whatever
  // exception guarantee F's move constructor provides - at minimum the
  // basic guarantee promises it remains destructible, but its value and
  // behavior are otherwise unspecified. Nothrow invocability only ensures
  // the attempted cleanup call itself won't throw, not that it retains the
  // original semantics.
  explicit scope_exit(F&& function) noexcept(
      std::is_nothrow_move_constructible_v<F>) try
      : function_(std::move(function)) {
  } catch (...) {
    function();
    throw;
  }

  // Moving allows ownership of the cleanup action to be transferred to another
  // guard while leaving the source inactive.
  scope_exit(scope_exit&& other) noexcept(
      std::is_nothrow_move_constructible_v<F>)
      : function_(std::move(other.function_)), active_(other.active_) {
    other.release();
  }

  SCOPE_EXIT_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_END

  // Move assignment is disabled because it would require defining how the
  // destination's existing cleanup action is handled.
  scope_exit& operator=(scope_exit&&) = delete;

  // NOLINTNEXTLINE(readability-identifier-naming)
  void release() noexcept { active_ = false; }

  ~scope_exit() noexcept {
    if (active_) {
      function_();
    }
  }

 private:
  F function_;
  bool active_ = true;
};

// Forces decay to a value type on CTAD; without this, constructing from an
// lvalue functor would deduce F as a reference type.
template <typename F>
scope_exit(F) -> scope_exit<F>;

#undef SCOPE_EXIT_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_BEGIN
#undef SCOPE_EXIT_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_END

#endif  // AMITG_FC_SCOPE_HPP_