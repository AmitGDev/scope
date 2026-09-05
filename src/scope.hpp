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

#include <exception>
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
// avoids either diagnostic on its respective compiler. scope_exit and
// scope_fail both genuinely need this function-try-block, since both must
// invoke the source callable if constructing the stored one fails.
// scope_success genuinely does not: construction failure isn't a success,
// so it has no compensating catch to trigger the false positive in the
// first place. See each class below for why.
#ifdef _MSC_VER
#define AMITG_FC_SCOPE_GUARD_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_BEGIN \
  __pragma(warning(push)) __pragma(warning(disable : 4297))
#define AMITG_FC_SCOPE_GUARD_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_END \
  __pragma(warning(pop))
#elif defined(__GNUC__) || defined(__clang__)
#define AMITG_FC_SCOPE_GUARD_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_BEGIN \
  _Pragma("GCC diagnostic push")                                    \
      _Pragma("GCC diagnostic ignored \"-Wterminate\"")
#define AMITG_FC_SCOPE_GUARD_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_END \
  _Pragma("GCC diagnostic pop")
#else
#define AMITG_FC_SCOPE_GUARD_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_BEGIN
#define AMITG_FC_SCOPE_GUARD_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_END
#endif

// Named to match std::experimental::scope_exit from the Library Fundamentals
// TS.
template <typename F>
class scope_exit final {  // NOLINT(readability-identifier-naming)
 public:
  // Rejects pathological direct instantiations such as scope_exit<void>
  // with a clean diagnostic, rather than the invocability check below
  // failing indirectly on an ill-formed "F&" for a non-object F.
  static_assert(std::is_object_v<F>);

  // The stored callable must not throw when invoked by the destructor.
  // This is an unconditional class invariant: every instance is destroyed
  // exactly once, and the destructor's own noexcept guarantee depends on
  // this holding regardless of which constructor built the object.
  static_assert(std::is_nothrow_invocable_v<F&>);

  // Copying would give two guards ownership of the same cleanup action,
  // causing it to run more than once. Copying is therefore disabled.
  scope_exit(const scope_exit&) = delete;
  scope_exit& operator=(const scope_exit&) = delete;

  AMITG_FC_SCOPE_GUARD_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_BEGIN

  // The catch block below invokes the callable through a const reference on
  // copy failure, so it must also be invocable in that const-qualified
  // form. That requirement is expressed as a constraint on this
  // constructor rather than a class-scope static_assert, since a
  // static_assert here would be checked whenever scope_exit<F> is
  // instantiated at all, rejecting a mutable-lambda F outright even if
  // only the F&& overload below is ever used for it.
  explicit scope_exit(const F& function) noexcept(
      std::is_nothrow_copy_constructible_v<F>)
    requires std::is_nothrow_invocable_v<const F&>
  try : function_(function) {
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
  // guard while leaving the source inactive. Constrained on
  // is_move_constructible_v<F> (satisfied by a copy constructor too, since
  // an rvalue binds to const F&) purely for a clearer diagnostic if F
  // supports neither; the constraint doesn't change which types compile.
  scope_exit(scope_exit&& other) noexcept(
      std::is_nothrow_move_constructible_v<F>)
    requires std::is_move_constructible_v<F>
      : function_(std::move(other.function_)), active_(other.active_) {
    other.release();
  }

  AMITG_FC_SCOPE_GUARD_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_END

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

// CTAD stores a decayed value type, preventing lvalue callables from
// producing reference-member specializations.
template <typename F>
scope_exit(F) -> scope_exit<std::decay_t<F>>;

// Named to match std::experimental::scope_fail from the Library Fundamentals
// TS. Unlike scope_exit, the callable fires only when this guard's scope is
// left because of an exception, not on ordinary return.
//
// The number of uncaught exceptions is recorded when the guard is created.
// If the count is greater when the guard is destroyed, an exception has
// been introduced and is still unwinding through this scope. This is the
// standard technique behind this kind of guard (popularized by Andrei
// Alexandrescu and Boost.Scope) and does not require the callable itself
// to inspect or rethrow anything.
template <typename F>
class scope_fail final {  // NOLINT(readability-identifier-naming)
 public:
  // Rejects pathological direct instantiations such as scope_fail<void>
  // with a clean diagnostic, rather than the invocability check below
  // failing indirectly on an ill-formed "F&" for a non-object F.
  static_assert(std::is_object_v<F>);

  // The callable must be noexcept-invocable because destruction is
  // noexcept and invoking the callable from the destructor must never
  // propagate an exception.
  static_assert(std::is_nothrow_invocable_v<F&>);

  scope_fail(const scope_fail&) = delete;
  scope_fail& operator=(const scope_fail&) = delete;

  AMITG_FC_SCOPE_GUARD_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_BEGIN

  // Failing to construct the stored callable is itself an exceptional
  // exit from this constructor, so - consistent with the "fail" semantics
  // this guard is named for - the source callable is still invoked before
  // the exception propagates, exactly as scope_exit does. See scope_exit's
  // matching constructor for why that const-invocability requirement is a
  // constraint on this constructor rather than a class-scope static_assert.
  explicit scope_fail(const F& function) noexcept(
      std::is_nothrow_copy_constructible_v<F>)
    requires std::is_nothrow_invocable_v<const F&>
  try : function_(function), exception_count_(std::uncaught_exceptions()) {
  } catch (...) {
    function();
    throw;
  }

  explicit scope_fail(F&& function) noexcept(
      std::is_nothrow_move_constructible_v<F>) try
      : function_(std::move(function)),
        exception_count_(std::uncaught_exceptions()) {
  } catch (...) {
    function();
    throw;
  }

  // See scope_exit's move constructor for why this constraint is here.
  scope_fail(scope_fail&& other) noexcept(
      std::is_nothrow_move_constructible_v<F>)
    requires std::is_move_constructible_v<F>
      : function_(std::move(other.function_)),
        exception_count_(other.exception_count_),
        active_(other.active_) {
    other.release();
  }

  AMITG_FC_SCOPE_GUARD_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_END

  scope_fail& operator=(scope_fail&&) = delete;

  // NOLINTNEXTLINE(readability-identifier-naming)
  void release() noexcept { active_ = false; }

  ~scope_fail() noexcept {
    if (active_ && std::uncaught_exceptions() > exception_count_) {
      function_();
    }
  }

 private:
  F function_;
  int exception_count_;
  bool active_ = true;
};

// Same rationale as scope_exit's deduction guide above: CTAD stores a
// decayed value type, preventing lvalue callables from producing
// reference-member specializations.
template <typename F>
scope_fail(F) -> scope_fail<std::decay_t<F>>;

// Named to match std::experimental::scope_success from the Library
// Fundamentals TS. Mirror image of scope_fail: the callable fires only when
// this guard's scope is left normally, i.e. no exception is unwinding
// through it. See scope_fail's class comment for how the uncaught-exception
// count is used to detect that.
//
// Construction failure is not a success, so this guard must NOT invoke the
// callable on that path - the opposite of scope_exit and scope_fail. A
// plain member-initializer list already gives that behavior for free (the
// exception simply propagates and no destructor for this object ever
// runs), so no function-try-block, no compensating catch, and therefore
// none of the noexcept-false-positive suppression scope_exit and scope_fail
// need is required here.
//
// One deliberate departure from the standard facility: the standard
// scope_success allows a potentially throwing callable and gives its
// destructor a matching conditional noexcept. This implementation instead
// requires a nonthrowing callable and an unconditionally noexcept
// destructor, the same as scope_exit and scope_fail, so that all three
// guards share one uniform, simpler-to-audit contract instead of
// scope_success alone being allowed to propagate an exception from
// destruction.
template <typename F>
class scope_success final {  // NOLINT(readability-identifier-naming)
 public:
  // Rejects pathological direct instantiations such as scope_success<void>
  // with a clean diagnostic, rather than the invocability check below
  // failing indirectly on an ill-formed "F&" for a non-object F.
  static_assert(std::is_object_v<F>);

  // See the class comment above: this project's scope_success requires a
  // nonthrowing callable, unlike the standard facility it's named after.
  static_assert(std::is_nothrow_invocable_v<F&>);

  scope_success(const scope_success&) = delete;
  scope_success& operator=(const scope_success&) = delete;

  explicit scope_success(const F& function) noexcept(
      std::is_nothrow_copy_constructible_v<F>)
      : function_(function), exception_count_(std::uncaught_exceptions()) {}

  explicit scope_success(F&& function) noexcept(
      std::is_nothrow_move_constructible_v<F>)
      : function_(std::move(function)),
        exception_count_(std::uncaught_exceptions()) {}

  // See scope_exit's move constructor for why this constraint is here.
  scope_success(scope_success&& other) noexcept(
      std::is_nothrow_move_constructible_v<F>)
    requires std::is_move_constructible_v<F>
      : function_(std::move(other.function_)),
        exception_count_(other.exception_count_),
        active_(other.active_) {
    other.release();
  }

  scope_success& operator=(scope_success&&) = delete;

  // NOLINTNEXTLINE(readability-identifier-naming)
  void release() noexcept { active_ = false; }

  ~scope_success() noexcept {
    if (active_ && std::uncaught_exceptions() <= exception_count_) {
      function_();
    }
  }

 private:
  F function_;
  int exception_count_;
  bool active_ = true;
};

// Same rationale as scope_exit's deduction guide above: CTAD stores a
// decayed value type, preventing lvalue callables from producing
// reference-member specializations.
template <typename F>
scope_success(F) -> scope_success<std::decay_t<F>>;

#undef AMITG_FC_SCOPE_GUARD_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_BEGIN
#undef AMITG_FC_SCOPE_GUARD_SUPPRESS_NOEXCEPT_FALSE_POSITIVE_END

#endif  // AMITG_FC_SCOPE_HPP_