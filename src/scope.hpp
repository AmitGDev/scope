#ifndef AMITGDEV_SCOPE_HPP_
#define AMITGDEV_SCOPE_HPP_

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
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
*/

#include <type_traits>
#include <utility>

// scope_exit uses a constructor-level try/catch because if
// constructing the stored callable fails, the source callable must still be
// invoked before the exception is rethrown.
//
// This can trigger a false-positive warning on each compiler family:
// - GCC may warn with -Wterminate because they cannot prove that the
//   catch's rethrow is unreachable, even when the guarded construction cannot
//   throw.
// - MSVC may warn with C4297 even though
//   std::is_nothrow_move/copy_constructible_v reports the special member as
//   noexcept; see
//   https://quuxplusone.github.io/blog/2023/04/17/noexcept-false-equals-default/
//
// Suppress these warnings locally because there is no known portable code shape
// that avoids them.
#ifdef _MSC_VER
#define AMITGDEV_SCOPE_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_BEGIN \
  __pragma(warning(push)) __pragma(warning(disable : 4297))
#define AMITGDEV_SCOPE_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_END \
  __pragma(warning(pop))

#elif defined(__GNUC__) && !defined(__clang__)
#define AMITGDEV_SCOPE_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_BEGIN \
  _Pragma("GCC diagnostic push")                              \
      _Pragma("GCC diagnostic ignored \"-Wterminate\"")
#define AMITGDEV_SCOPE_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_END \
  _Pragma("GCC diagnostic pop")

#else
#define AMITGDEV_SCOPE_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_BEGIN
#define AMITGDEV_SCOPE_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_END
#endif

namespace amitgdev {

// SCOPE EXIT CLASS
// Named to match std::experimental::scope_exit from the Library Fundamentals
// TS. Invokes the callable when the guard is destroyed, unless released.
template <typename F>
class scope_exit final {  // NOLINT(readability-identifier-naming)
 public:
  // Rejects pathological direct instantiations such as scope_exit<void> with a
  // clear diagnostic, rather than failing indirectly in the invocability check.
  static_assert(std::is_object_v<F>);

  // The callable is invoked by a noexcept destructor, so every stored callable
  // must be nonthrowing. This is an unconditional class invariant.
  static_assert(std::is_nothrow_invocable_v<F&>);

  // Copying would give two guards ownership of the same cleanup action.
  scope_exit(const scope_exit&) = delete;
  scope_exit& operator=(const scope_exit&) = delete;

  AMITGDEV_SCOPE_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_BEGIN

  // The catch below invokes the source callable through a const reference.
  // Keep this requirement on the constructor: a mutable callable may still be
  // valid when only the F&& constructor is used.
  explicit scope_exit(const F& function) noexcept(
      std::is_nothrow_copy_constructible_v<F>)
    requires std::is_nothrow_invocable_v<const F&>
  try : function_(function) {
  } catch (...) {
    function();
    throw;
  }

  // If constructing the stored callable fails, invoke the source callable
  // before propagating the exception so its resource can still be cleaned up.
  //
  // This is best-effort: the callable's state after a throwing move is governed
  // by F's exception guarantee. Nothrow invocability only guarantees that the
  // cleanup attempt itself cannot throw.
  explicit scope_exit(F&& function) noexcept(
      std::is_nothrow_move_constructible_v<F>) try
      : function_(std::move(function)) {
  } catch (...) {
    function();
    throw;
  }

  // Moving transfers ownership of the cleanup action and deactivates the
  // source. The constraint provides a clearer diagnostic when F cannot be
  // constructed from an rvalue.
  scope_exit(scope_exit&& other) noexcept(
      std::is_nothrow_move_constructible_v<F>)
    requires std::is_move_constructible_v<F>
      : function_(std::move(other.function_)), active_(other.active_) {
    other.release();
  }

  AMITGDEV_SCOPE_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_END

  // Move assignment would require defining what happens to the destination's
  // existing cleanup action, so it is deliberately disabled.
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

// CTAD stores a decayed value type, preventing lvalue callables from producing
// reference-member specializations.
template <typename F>
scope_exit(F) -> scope_exit<std::decay_t<F>>;

}  // namespace amitgdev

#undef AMITGDEV_SCOPE_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_BEGIN
#undef AMITGDEV_SCOPE_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_END

#endif  // AMITGDEV_SCOPE_HPP_