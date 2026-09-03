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

static bool TestNormalScopeExit() {
  bool executed = false;

  {
    const scope_exit guard([&executed] noexcept { executed = true; });
    std::cout << "  Leaving scope normally...\n";
  }

  return executed;
}

static int TestEarlyReturn(bool& executed) {
  const scope_exit guard([&executed] noexcept { executed = true; });
  return 42;
}

static bool TestExceptionUnwinding() {
  bool executed = false;

  try {
    const scope_exit guard([&executed] noexcept { executed = true; });
    throw std::runtime_error("test exception");
  } catch (const std::exception& exception) {
    std::cout << "  Caught: " << exception.what() << '\n';
  }

  return executed;
}

static int TestDestructionOrder() {
  int order = 0;

  {
    const scope_exit first([&order] noexcept { order = (order * 10) + 1; });
    const scope_exit second([&order] noexcept { order = (order * 10) + 2; });
    const scope_exit third([&order] noexcept { order = (order * 10) + 3; });
  }

  return order;
}

static bool TestCallableOwnership() {
  bool executed = false;
  auto callable = [&executed] noexcept { executed = true; };

  { const scope_exit guard(std::move(callable)); }

  return executed;
}

static bool TestMoveConstruction() {
  bool executed = false;

  {
    scope_exit original(MoveOnlyCounter{executed});
    const scope_exit moved(std::move(original));
  }

  return executed;
}

static bool TestMovedFromInactive() {
  bool executed = false;

  {
    scope_exit original(MoveOnlyCounter{executed});
    const scope_exit moved(std::move(original));
    (void)moved;
  }

  return executed;
}

static bool TestRelease() {
  bool executed = false;

  {
    scope_exit guard([&executed] noexcept { executed = true; });
    guard.release();
  }

  return !executed;
}

static void TestCompileTimeProperties() {
  // The non-const local is intentional: preserving the lambda's non-const
  // type prevents scope_exit from storing a const-qualified callable.
  // NOLINTNEXTLINE(misc-const-correctness)
  auto noexcept_callable = [] noexcept {};
  using NoexceptGuard = scope_exit<decltype(noexcept_callable)>;
  using MoveOnlyGuard = scope_exit<MoveOnlyCounter>;
  using CopyableGuard = scope_exit<CopyableNothrowCallable>;

  static_assert(!std::is_copy_constructible_v<NoexceptGuard>);
  static_assert(!std::is_copy_assignable_v<NoexceptGuard>);
  static_assert(!std::is_move_assignable_v<NoexceptGuard>);
  static_assert(std::is_nothrow_move_constructible_v<NoexceptGuard>);

  static_assert(!std::is_copy_constructible_v<MoveOnlyGuard>);
  static_assert(std::is_move_constructible_v<MoveOnlyGuard>);

  static_assert(std::is_move_constructible_v<CopyableGuard>);
  static_assert(std::is_nothrow_move_constructible_v<CopyableGuard>);
}

// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
  try {
    TestCompileTimeProperties();

    std::cout << "scope_exit C++23 demo\n\n";

    std::cout << "1. Normal scope exit\n";
    const bool normal_exit = TestNormalScopeExit();
    std::cout << "  Callback executed: " << std::boolalpha << normal_exit
              << "\n\n";

    std::cout << "2. Early return\n";
    bool early_return_executed = false;
    const int return_value = TestEarlyReturn(early_return_executed);
    std::cout << "  Return value: " << return_value << '\n';
    std::cout << "  Callback executed: " << early_return_executed << "\n\n";

    std::cout << "3. Exception unwinding\n";
    const bool exception_executed = TestExceptionUnwinding();
    std::cout << "  Callback executed: " << exception_executed << "\n\n";

    std::cout << "4. Multiple guards - reverse destruction order\n";
    const int order = TestDestructionOrder();
    std::cout << "  Execution order: " << order << "\n\n";

    std::cout << "5. Callable ownership\n";
    const bool callable_executed = TestCallableOwnership();
    std::cout << "  Callable moved into scope_exit: " << callable_executed
              << "\n\n";

    std::cout << "6. Move construction\n";
    const bool move_construction_executed = TestMoveConstruction();
    std::cout << "  Moved guard executed: " << move_construction_executed
              << "\n\n";

    std::cout << "7. Moved-from inactive\n";
    const bool moved_from_inactive = TestMovedFromInactive();
    std::cout << "  Moved-from guard stayed inactive: " << moved_from_inactive
              << "\n\n";

    std::cout << "8. Release behavior\n";
    const bool release_ok = TestRelease();
    std::cout << "  Release prevented execution: " << release_ok << "\n\n";

    std::cout << "\nDone - OK\n";
    return 0;
  } catch (...) {
    return 1;
  }
}