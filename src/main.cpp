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
}

// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
  try {
    TestCompileTimeProperties();

    RunScopeExitDemo();

    std::cout << "\nDone - OK\n";
    return 0;
  } catch (...) {
    return 1;
  }
}
