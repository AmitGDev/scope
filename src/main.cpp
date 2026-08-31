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

// scope_exit only requires F to be nothrow move constructible and nothrow
// invocable; it never copies or moves the stored callable after
// construction. These two helper types exist purely to instantiate
// scope_exit with a move-only and a copyable F respectively, to confirm the
// class template accepts both.
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

  // The guard is pinned to its scope regardless of F: copy and move are
  // unconditionally deleted, even when F itself is copyable.
  static_assert(!std::is_copy_constructible_v<ExitNoexceptGuard>);
  static_assert(!std::is_copy_assignable_v<ExitNoexceptGuard>);
  static_assert(!std::is_move_constructible_v<ExitNoexceptGuard>);
  static_assert(!std::is_move_assignable_v<ExitNoexceptGuard>);
  static_assert(!std::is_move_constructible_v<ExitMoveOnlyGuard>);
  static_assert(!std::is_move_constructible_v<ExitCopyableGuard>);
  static_assert(!std::is_copy_constructible_v<ExitCopyableGuard>);
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
