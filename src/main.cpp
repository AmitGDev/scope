/*
    main.cpp
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
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "scope.hpp"

namespace {

// A plain nothrow-invocable callable makes no assumptions about which guard
// invokes it, so one pair of helper types covers both the noexcept-only and
// the move-only/copyable construction paths of scope_exit.
struct MoveOnlyCounter final {
  bool* executed = nullptr;

  MoveOnlyCounter() = default;

  explicit MoveOnlyCounter(bool& flag) noexcept : executed(&flag) {}

  MoveOnlyCounter(const MoveOnlyCounter&) = delete;
  MoveOnlyCounter& operator=(const MoveOnlyCounter&) = delete;

  MoveOnlyCounter(MoveOnlyCounter&& other) noexcept
      : executed(std::exchange(other.executed, nullptr)) {}

  MoveOnlyCounter& operator=(MoveOnlyCounter&&) = delete;

  ~MoveOnlyCounter() = default;

  void operator()() const noexcept {
    if (executed != nullptr) {
      *executed = true;
    }
  }
};

struct CopyableNothrowCallable final {
  bool* executed = nullptr;

  CopyableNothrowCallable() = default;

  explicit CopyableNothrowCallable(bool& flag) noexcept : executed(&flag) {}

  CopyableNothrowCallable(const CopyableNothrowCallable&) = default;
  CopyableNothrowCallable& operator=(const CopyableNothrowCallable&) = default;
  CopyableNothrowCallable(CopyableNothrowCallable&&) = default;
  CopyableNothrowCallable& operator=(CopyableNothrowCallable&&) = default;
  ~CopyableNothrowCallable() = default;

  void operator()() const noexcept {
    if (executed != nullptr) {
      *executed = true;
    }
  }
};

// Gives scope_exit(F&&)'s catch clause a genuine construction failure to
// recover from.
struct ThrowingMoveCallable final {
  bool* executed = nullptr;

  explicit ThrowingMoveCallable(bool& flag) noexcept : executed(&flag) {}

  ThrowingMoveCallable(const ThrowingMoveCallable&) = delete;
  ThrowingMoveCallable& operator=(const ThrowingMoveCallable&) = delete;

  // Deliberately throwing and deliberately not noexcept: this type exists
  // solely to give scope_exit(F&&)'s catch clause a real construction
  // failure to recover from, so both properties are load-bearing here, not
  // oversights.
  // NOLINTNEXTLINE(bugprone-exception-escape,cppcoreguidelines-noexcept-move-operations,performance-noexcept-move-constructor)
  ThrowingMoveCallable(ThrowingMoveCallable&& other)
      : executed(other.executed) {
    throw std::runtime_error("simulated move failure");
  }

  ThrowingMoveCallable& operator=(ThrowingMoveCallable&&) = delete;
  ~ThrowingMoveCallable() = default;

  void operator()() const noexcept {
    if (executed != nullptr) {
      *executed = true;
    }
  }
};

// A destructor that itself constructs and destroys a scope_fail and a
// scope_success guard, with both scopes completing normally (no new throw).
// Used to prove the uncaught-exceptions-count design: when this destructor
// runs while an unrelated outer exception is still unwinding, each nested
// guard's own exception_count_ snapshot already reflects that outer
// exception, so from each guard's own local perspective nothing new went
// wrong - scope_fail must stay silent and scope_success must still fire,
// even though the process is, technically, mid-unwind.
// Bundles the two output flags into one named-field type so
// DestructorProbe's constructor takes a single parameter instead of two
// adjacent bool& parameters that would be easy to swap by mistake.
struct DestructorProbeFlags {
  bool* fail_stayed_silent = nullptr;
  bool* success_still_fired = nullptr;
};

struct DestructorProbe final {
  DestructorProbeFlags flags;

  explicit DestructorProbe(DestructorProbeFlags result_flags) noexcept
      : flags(result_flags) {}

  DestructorProbe(const DestructorProbe&) = delete;
  DestructorProbe& operator=(const DestructorProbe&) = delete;
  DestructorProbe(DestructorProbe&&) = delete;
  DestructorProbe& operator=(DestructorProbe&&) = delete;

  ~DestructorProbe() {
    bool fail_fired = false;
    {
      const amitgdev::scope_fail fail_guard(
          [&fail_fired] noexcept { fail_fired = true; });
    }
    *flags.fail_stayed_silent = !fail_fired;

    bool success_fired = false;
    {
      const amitgdev::scope_success success_guard(
          [&success_fired] noexcept { success_fired = true; });
    }
    *flags.success_still_fired = success_fired;
  }
};

}  // namespace

// ===========================================================================
// scope_exit
// ===========================================================================

static bool TestExitNormalScopeExit() {
  bool executed = false;

  {
    const amitgdev::scope_exit guard([&executed] noexcept { executed = true; });
    std::cout << "  Leaving scope normally...\n";
  }

  return executed;
}

static int TestExitEarlyReturn(bool& executed) {
  const amitgdev::scope_exit guard([&executed] noexcept { executed = true; });
  return 42;
}

static bool TestExitExceptionUnwinding() {
  bool executed = false;

  try {
    const amitgdev::scope_exit guard([&executed] noexcept { executed = true; });
    throw std::runtime_error("test exception");
  } catch (const std::exception& exception) {
    std::cout << "  Caught: " << exception.what() << '\n';
  }

  return executed;
}

static int TestExitDestructionOrder() {
  int order = 0;

  {
    const amitgdev::scope_exit first(
        [&order] noexcept { order = (order * 10) + 1; });
    const amitgdev::scope_exit second(
        [&order] noexcept { order = (order * 10) + 2; });
    const amitgdev::scope_exit third(
        [&order] noexcept { order = (order * 10) + 3; });
  }

  return order;
}

static bool TestExitCallableOwnership() {
  bool executed = false;
  auto callable = [&executed] noexcept { executed = true; };

  { const amitgdev::scope_exit guard(std::move(callable)); }

  return executed;
}

static bool TestExitMoveConstruction() {
  bool executed = false;

  {
    amitgdev::scope_exit original(MoveOnlyCounter{executed});
    const amitgdev::scope_exit moved(std::move(original));
  }

  return executed;
}

static bool TestExitMovedFromInactive() {
  bool executed = false;

  {
    amitgdev::scope_exit original(MoveOnlyCounter{executed});
    const amitgdev::scope_exit moved(std::move(original));
    (void)moved;
  }

  return executed;
}

static bool TestExitRelease() {
  bool executed = false;

  {
    amitgdev::scope_exit guard([&executed] noexcept { executed = true; });
    guard.release();
  }

  return !executed;
}

static bool TestExitCopyConstruction() {
  bool executed = false;
  const CopyableNothrowCallable callable(executed);

  {
    // callable is a named lvalue, so this can only bind the const F&
    // constructor overload - F&& requires an rvalue. This is the only test
    // in this suite that exercises that overload (and, via CTAD, the
    // deduction guide's lvalue-decay case).
    const amitgdev::scope_exit guard(callable);
  }

  return executed;
}

static bool TestExitThrowingConstruction() {
  bool executed = false;
  bool caught = false;

  try {
    ThrowingMoveCallable callable(executed);
    const amitgdev::scope_exit guard(std::move(callable));
  } catch (const std::runtime_error&) {
    caught = true;
  }

  // Moving the callable into the guard failed, but the constructor's catch
  // clause must still invoke the source callable before rethrowing.
  return caught && executed;
}

static void RunScopeExitDemo() {
  std::cout << "scope_exit C++23 demo\n\n";

  std::cout << "1. Normal scope exit\n";
  const bool normal_exit = TestExitNormalScopeExit();
  std::cout << "  Callback executed: " << std::boolalpha << normal_exit
            << "\n\n";

  std::cout << "2. Early return\n";
  bool early_return_executed = false;
  const int return_value = TestExitEarlyReturn(early_return_executed);
  std::cout << "  Return value: " << return_value << '\n';
  std::cout << "  Callback executed: " << early_return_executed << "\n\n";

  std::cout << "3. Exception unwinding\n";
  const bool exception_executed = TestExitExceptionUnwinding();
  std::cout << "  Callback executed: " << exception_executed << "\n\n";

  std::cout << "4. Multiple guards - reverse destruction order\n";
  const int order = TestExitDestructionOrder();
  std::cout << "  Execution order: " << order << "\n\n";

  std::cout << "5. Callable ownership\n";
  const bool callable_executed = TestExitCallableOwnership();
  std::cout << "  Callable moved into scope_exit: " << callable_executed
            << "\n\n";

  std::cout << "6. Move construction\n";
  const bool move_construction_executed = TestExitMoveConstruction();
  std::cout << "  Moved guard executed: " << move_construction_executed
            << "\n\n";

  std::cout << "7. Moved-from inactive\n";
  const bool moved_from_inactive = TestExitMovedFromInactive();
  std::cout << "  Moved-from guard stayed inactive: " << moved_from_inactive
            << "\n\n";

  std::cout << "8. Release behavior\n";
  const bool release_ok = TestExitRelease();
  std::cout << "  Release prevented execution: " << release_ok << "\n\n";

  std::cout << "9. Copy construction (const F&)\n";
  const bool copy_construction_executed = TestExitCopyConstruction();
  std::cout << "  Callback executed: " << copy_construction_executed << "\n\n";

  std::cout << "10. Throwing construction\n";
  const bool throwing_construction_ok = TestExitThrowingConstruction();
  std::cout << "  Exception propagated and source callable still ran: "
            << throwing_construction_ok << "\n\n";
}

// ===========================================================================
// scope_fail
// ===========================================================================
//
// Same scenarios as scope_exit, but the expected outcome is flipped: the
// callback should run only when the guarded block exits via an exception,
// never on a normal exit.

static bool TestFailNormalScopeExit() {
  bool executed = false;

  {
    const amitgdev::scope_fail guard([&executed] noexcept { executed = true; });
    std::cout << "  Leaving scope normally...\n";
  }

  return !executed;  // Expect no exception -> callback must not have run.
}

static int TestFailEarlyReturn(bool& executed) {
  const amitgdev::scope_fail guard([&executed] noexcept { executed = true; });
  return 42;
}

static bool TestFailExceptionUnwinding() {
  bool executed = false;

  try {
    const amitgdev::scope_fail guard([&executed] noexcept { executed = true; });
    throw std::runtime_error("test exception");
  } catch (const std::exception& exception) {
    std::cout << "  Caught: " << exception.what() << '\n';
  }

  return executed;  // Expect an exception -> callback must have run.
}

static int TestFailDestructionOrder() {
  int order = 0;

  try {
    const amitgdev::scope_fail first(
        [&order] noexcept { order = (order * 10) + 1; });
    const amitgdev::scope_fail second(
        [&order] noexcept { order = (order * 10) + 2; });
    const amitgdev::scope_fail third(
        [&order] noexcept { order = (order * 10) + 3; });
    throw std::runtime_error("trigger rollback");
  } catch (const std::exception& exception) {
    std::cout << "  Triggered rollback: " << exception.what() << '\n';
  }

  return order;
}

static bool TestFailCallableOwnership() {
  bool executed = false;
  auto callable = [&executed] noexcept { executed = true; };

  try {
    const amitgdev::scope_fail guard(std::move(callable));
    throw std::runtime_error("trigger rollback");
  } catch (const std::exception& exception) {
    std::cout << "  Triggered rollback: " << exception.what() << '\n';
  }

  return executed;
}

static bool TestFailMoveConstruction() {
  bool executed = false;

  try {
    amitgdev::scope_fail original(MoveOnlyCounter{executed});
    const amitgdev::scope_fail moved(std::move(original));
    throw std::runtime_error("trigger rollback");
  } catch (const std::exception& exception) {
    std::cout << "  Triggered rollback: " << exception.what() << '\n';
  }

  return executed;
}

static bool TestFailMovedFromInactive() {
  bool executed = false;

  try {
    amitgdev::scope_fail original(MoveOnlyCounter{executed});
    const amitgdev::scope_fail moved(std::move(original));
    (void)moved;
    throw std::runtime_error("trigger rollback");
  } catch (const std::exception& exception) {
    std::cout << "  Triggered rollback: " << exception.what() << '\n';
  }

  return executed;
}

static bool TestFailRelease() {
  bool executed = false;

  try {
    amitgdev::scope_fail guard([&executed] noexcept { executed = true; });
    guard.release();
    throw std::runtime_error("trigger rollback");
  } catch (const std::exception& exception) {
    std::cout << "  Triggered rollback: " << exception.what() << '\n';
  }

  return !executed;
}

static bool TestFailCopyConstruction() {
  bool executed = false;
  const CopyableNothrowCallable callable(executed);

  try {
    // callable is a named lvalue, so this can only bind the const F&
    // overload - the only test in this suite that exercises it.
    const amitgdev::scope_fail guard(callable);
    throw std::runtime_error("trigger rollback");
  } catch (const std::exception& exception) {
    std::cout << "  Triggered rollback: " << exception.what() << '\n';
  }

  return executed;
}

static bool TestFailThrowingConstruction() {
  bool executed = false;
  bool caught = false;

  try {
    ThrowingMoveCallable callable(executed);
    const amitgdev::scope_fail guard(std::move(callable));
  } catch (const std::runtime_error&) {
    caught = true;
  }

  // Moving the callable into the guard failed, but the constructor's catch
  // clause must still invoke the source callable before rethrowing - this
  // happens before exception_count_ is even initialized.
  return caught && executed;
}

static void RunScopeFailDemo() {
  std::cout << "scope_fail C++23 demo\n\n";

  std::cout << "1. Normal scope exit\n";
  const bool normal_exit_skipped = TestFailNormalScopeExit();
  std::cout << "  Callback correctly skipped: " << std::boolalpha
            << normal_exit_skipped << "\n\n";

  std::cout << "2. Early return\n";
  bool early_return_executed = false;
  const int return_value = TestFailEarlyReturn(early_return_executed);
  std::cout << "  Return value: " << return_value << '\n';
  std::cout << "  Callback executed: " << early_return_executed << "\n\n";

  std::cout << "3. Exception unwinding\n";
  const bool exception_executed = TestFailExceptionUnwinding();
  std::cout << "  Callback executed: " << exception_executed << "\n\n";

  std::cout << "4. Multiple guards - reverse destruction order\n";
  const int order = TestFailDestructionOrder();
  std::cout << "  Execution order: " << order << "\n\n";

  std::cout << "5. Callable ownership\n";
  const bool callable_executed = TestFailCallableOwnership();
  std::cout << "  Callable moved into scope_fail: " << callable_executed
            << "\n\n";

  std::cout << "6. Move construction\n";
  const bool move_construction_executed = TestFailMoveConstruction();
  std::cout << "  Moved guard executed: " << move_construction_executed
            << "\n\n";

  std::cout << "7. Moved-from inactive\n";
  const bool moved_from_inactive = TestFailMovedFromInactive();
  std::cout << "  Moved-to guard executed, moved-from stayed inactive: "
            << moved_from_inactive << "\n\n";

  std::cout << "8. Release behavior\n";
  const bool release_ok = TestFailRelease();
  std::cout << "  Release prevented execution despite exception: " << release_ok
            << "\n\n";

  std::cout << "9. Copy construction (const F&)\n";
  const bool copy_construction_executed = TestFailCopyConstruction();
  std::cout << "  Callback executed: " << copy_construction_executed << "\n\n";

  std::cout << "10. Throwing construction\n";
  const bool throwing_construction_ok = TestFailThrowingConstruction();
  std::cout << "  Exception propagated and source callable still ran: "
            << throwing_construction_ok << "\n\n";
}

// ===========================================================================
// scope_success
// ===========================================================================
//
// Same scenarios again, mirrored the other way: the callback should run
// only when the guarded block exits normally, never via an exception.

static bool TestSuccessNormalScopeExit() {
  bool executed = false;

  {
    const amitgdev::scope_success guard(
        [&executed] noexcept { executed = true; });
    std::cout << "  Leaving scope normally...\n";
  }

  return executed;  // Expect no exception -> callback must have run.
}

static int TestSuccessEarlyReturn(bool& executed) {
  const amitgdev::scope_success guard(
      [&executed] noexcept { executed = true; });
  return 42;
}

static bool TestSuccessExceptionUnwinding() {
  bool executed = false;

  try {
    const amitgdev::scope_success guard(
        [&executed] noexcept { executed = true; });
    throw std::runtime_error("test exception");
  } catch (const std::exception& exception) {
    std::cout << "  Caught: " << exception.what() << '\n';
  }

  return !executed;  // Expect an exception -> callback must not have run.
}

static int TestSuccessDestructionOrder() {
  int order = 0;

  {
    const amitgdev::scope_success first(
        [&order] noexcept { order = (order * 10) + 1; });
    const amitgdev::scope_success second(
        [&order] noexcept { order = (order * 10) + 2; });
    const amitgdev::scope_success third(
        [&order] noexcept { order = (order * 10) + 3; });
  }

  return order;
}

static bool TestSuccessCallableOwnership() {
  bool executed = false;
  auto callable = [&executed] noexcept { executed = true; };

  { const amitgdev::scope_success guard(std::move(callable)); }

  return executed;
}

static bool TestSuccessMoveConstruction() {
  bool executed = false;

  {
    amitgdev::scope_success original(MoveOnlyCounter{executed});
    const amitgdev::scope_success moved(std::move(original));
  }

  return executed;
}

static bool TestSuccessMovedFromInactive() {
  bool executed = false;

  {
    amitgdev::scope_success original(MoveOnlyCounter{executed});
    const amitgdev::scope_success moved(std::move(original));
    (void)moved;
  }

  return executed;
}

static bool TestSuccessRelease() {
  bool executed = false;

  {
    amitgdev::scope_success guard([&executed] noexcept { executed = true; });
    guard.release();
  }

  return !executed;
}

static bool TestSuccessCopyConstruction() {
  bool executed = false;
  const CopyableNothrowCallable callable(executed);

  {
    // callable is a named lvalue, so this can only bind the const F&
    // overload - the only test in this suite that exercises it.
    const amitgdev::scope_success guard(callable);
  }

  return executed;
}

static bool TestSuccessThrowingConstructionSkipsCallback() {
  bool executed = false;
  bool caught = false;

  try {
    ThrowingMoveCallable callable(executed);
    const amitgdev::scope_success guard(std::move(callable));
  } catch (const std::runtime_error&) {
    caught = true;
  }

  // Unlike scope_exit and scope_fail, scope_success has no compensating
  // catch: construction failure is not success, so the callback must NOT
  // have run. This is the opposite expectation from the other two guards
  // and is the easiest thing to break by copy-pasting their pattern here.
  return caught && !executed;
}

static void RunScopeSuccessDemo() {
  std::cout << "scope_success C++23 demo\n\n";

  std::cout << "1. Normal scope exit\n";
  const bool normal_exit = TestSuccessNormalScopeExit();
  std::cout << "  Callback executed: " << std::boolalpha << normal_exit
            << "\n\n";

  std::cout << "2. Early return\n";
  bool early_return_executed = false;
  const int return_value = TestSuccessEarlyReturn(early_return_executed);
  std::cout << "  Return value: " << return_value << '\n';
  std::cout << "  Callback executed: " << early_return_executed << "\n\n";

  std::cout << "3. Exception unwinding\n";
  const bool exception_skipped = TestSuccessExceptionUnwinding();
  std::cout << "  Callback correctly skipped: " << exception_skipped << "\n\n";

  std::cout << "4. Multiple guards - reverse destruction order\n";
  const int order = TestSuccessDestructionOrder();
  std::cout << "  Execution order: " << order << "\n\n";

  std::cout << "5. Callable ownership\n";
  const bool callable_executed = TestSuccessCallableOwnership();
  std::cout << "  Callable moved into scope_success: " << callable_executed
            << "\n\n";

  std::cout << "6. Move construction\n";
  const bool move_construction_executed = TestSuccessMoveConstruction();
  std::cout << "  Moved guard executed: " << move_construction_executed
            << "\n\n";

  std::cout << "7. Moved-from inactive\n";
  const bool moved_from_inactive = TestSuccessMovedFromInactive();
  std::cout << "  Moved-from guard stayed inactive: " << moved_from_inactive
            << "\n\n";

  std::cout << "8. Release behavior\n";
  const bool release_ok = TestSuccessRelease();
  std::cout << "  Release prevented execution: " << release_ok << "\n\n";

  std::cout << "9. Copy construction (const F&)\n";
  const bool copy_construction_executed = TestSuccessCopyConstruction();
  std::cout << "  Callback executed: " << copy_construction_executed << "\n\n";

  std::cout << "10. Throwing construction skips the callback\n";
  const bool throwing_construction_ok =
      TestSuccessThrowingConstructionSkipsCallback();
  std::cout << "  Exception propagated and callback correctly did not run: "
            << throwing_construction_ok << "\n\n";
}

// ===========================================================================
// Nested guards during unrelated exception unwinding
// ===========================================================================
//
// The key correctness property of the uncaught-exceptions-count design: a
// guard's exception_count_ snapshot is local to its own scope, so a guard
// constructed while some unrelated outer exception is already unwinding
// must judge its OWN exit, not the ambient unwinding state. A naive
// "is any exception currently propagating" check would get this wrong.

static bool TestNestedGuardsDuringUnwinding() {
  bool fail_stayed_silent = false;
  bool success_still_fired = false;

  try {
    const DestructorProbe probe(DestructorProbeFlags{
        .fail_stayed_silent = &fail_stayed_silent,
        .success_still_fired = &success_still_fired,
    });
    throw std::runtime_error("outer exception");
  } catch (const std::exception& exception) {
    std::cout << "  Outer exception caught: " << exception.what() << '\n';
  }

  return fail_stayed_silent && success_still_fired;
}

static void RunNestedGuardsDemo() {
  std::cout << "Nested guards during unrelated unwinding\n\n";

  const bool passed = TestNestedGuardsDuringUnwinding();
  std::cout << "  scope_fail stayed silent and scope_success still fired: "
            << std::boolalpha << passed << "\n\n";
}

// ===========================================================================
// Compile-time properties
// ===========================================================================

static void TestCompileTimeProperties() {
  // The non-const local is intentional: preserving the lambda's non-const
  // type prevents the guard from storing a const-qualified callable.
  // NOLINTNEXTLINE(misc-const-correctness)
  auto noexcept_callable = [] noexcept {};

  using ExitNoexceptGuard = amitgdev::scope_exit<decltype(noexcept_callable)>;
  using ExitMoveOnlyGuard = amitgdev::scope_exit<MoveOnlyCounter>;
  using ExitCopyableGuard = amitgdev::scope_exit<CopyableNothrowCallable>;

  static_assert(!std::is_copy_constructible_v<ExitNoexceptGuard>);
  static_assert(!std::is_copy_assignable_v<ExitNoexceptGuard>);
  static_assert(!std::is_move_assignable_v<ExitNoexceptGuard>);
  static_assert(std::is_nothrow_move_constructible_v<ExitNoexceptGuard>);
  static_assert(!std::is_copy_constructible_v<ExitMoveOnlyGuard>);
  static_assert(std::is_move_constructible_v<ExitMoveOnlyGuard>);
  static_assert(std::is_move_constructible_v<ExitCopyableGuard>);
  static_assert(std::is_nothrow_move_constructible_v<ExitCopyableGuard>);

  using FailNoexceptGuard = amitgdev::scope_fail<decltype(noexcept_callable)>;
  using FailMoveOnlyGuard = amitgdev::scope_fail<MoveOnlyCounter>;
  using FailCopyableGuard = amitgdev::scope_fail<CopyableNothrowCallable>;

  static_assert(!std::is_copy_constructible_v<FailNoexceptGuard>);
  static_assert(!std::is_copy_assignable_v<FailNoexceptGuard>);
  static_assert(!std::is_move_assignable_v<FailNoexceptGuard>);
  static_assert(std::is_nothrow_move_constructible_v<FailNoexceptGuard>);
  static_assert(!std::is_copy_constructible_v<FailMoveOnlyGuard>);
  static_assert(std::is_move_constructible_v<FailMoveOnlyGuard>);
  static_assert(std::is_move_constructible_v<FailCopyableGuard>);
  static_assert(std::is_nothrow_move_constructible_v<FailCopyableGuard>);

  using SuccessNoexceptGuard =
      amitgdev::scope_success<decltype(noexcept_callable)>;
  using SuccessMoveOnlyGuard = amitgdev::scope_success<MoveOnlyCounter>;
  using SuccessCopyableGuard = amitgdev::scope_success<CopyableNothrowCallable>;

  static_assert(!std::is_copy_constructible_v<SuccessNoexceptGuard>);
  static_assert(!std::is_copy_assignable_v<SuccessNoexceptGuard>);
  static_assert(!std::is_move_assignable_v<SuccessNoexceptGuard>);
  static_assert(std::is_nothrow_move_constructible_v<SuccessNoexceptGuard>);
  static_assert(!std::is_copy_constructible_v<SuccessMoveOnlyGuard>);
  static_assert(std::is_move_constructible_v<SuccessMoveOnlyGuard>);
  static_assert(std::is_move_constructible_v<SuccessCopyableGuard>);
  static_assert(std::is_nothrow_move_constructible_v<SuccessCopyableGuard>);
}

// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
  try {
    TestCompileTimeProperties();

    RunScopeExitDemo();
    std::cout << "----------------------------------------\n\n";
    RunScopeFailDemo();
    std::cout << "----------------------------------------\n\n";
    RunScopeSuccessDemo();
    std::cout << "----------------------------------------\n\n";
    RunNestedGuardsDemo();

    std::cout << "\nDone - OK\n";
    return 0;
  } catch (...) {
    return 1;
  }
}
