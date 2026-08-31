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
#include <utility>

#include "scope.hpp"

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

// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
  try {
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

    std::cout << "\nDone - OK\n";
    return 0;
  } catch (...) {
    return 1;
  }
}