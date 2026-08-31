# scope_exit

`scope_exit` is a small C++23 RAII utility that executes a callable when the enclosing scope is exited.

Its purpose is to provide a simple, explicit mechanism for expressing cleanup or rollback actions that must happen regardless of how control leaves a scope.

## Purpose

Resource-management code often needs an action to be performed when leaving a scope:

* Releasing a resource.
* Restoring state.
* Unlocking a manually managed resource.
* Removing a temporary file.
* Completing a rollback operation.
* Performing cleanup when returning early.
* Performing cleanup during exception unwinding.

`scope_exit` provides this behavior without requiring a separate named cleanup function or manually duplicating the cleanup operation across multiple exit paths.

The implementation is intentionally small. It provides one responsibility: **invoke a callable when the `scope_exit` object is destroyed.**

## Basic Usage

```cpp
#include "scope.hpp"

void Function() {
  Resource resource = AcquireResource();

  scope_exit cleanup([&resource]() noexcept {
    ReleaseResource(resource);
  });

  // Use resource...
}
```

When `Function` leaves the scope, `cleanup` is destroyed and its callable is invoked.

The cleanup therefore happens on every normal scope exit:

```text
scope entered
     |
     v
scope_exit constructed
     |
     v
use resource
     |
     v
scope exited
     |
     v
cleanup invoked
```

## Early Return

The cleanup is also performed when the scope is left through an early return.

```cpp
int Function() {
  scope_exit cleanup([]() noexcept {
    Cleanup();
  });

  if (condition) {
    return 42;
  }

  return 0;
}
```

The callback executes before the function returns.

This makes cleanup independent of the number of return paths in the function.

## Exception Unwinding

The callback is also executed during normal C++ stack unwinding.

```cpp
void Function() {
  scope_exit cleanup([]() noexcept {
    Cleanup();
  });

  throw std::runtime_error("failure");
}
```

When the exception causes the scope to be unwound, the `scope_exit` destructor runs and invokes the cleanup callback.

## `noexcept` Requirement

The callable must be nothrow-invocable:

```cpp
static_assert(std::is_nothrow_move_constructible_v<F>);
static_assert(std::is_nothrow_invocable_v<F&>);
```

Consequently, this is valid:

```cpp
scope_exit cleanup([]() noexcept {
  Cleanup();
});
```

while a potentially throwing callable is rejected:

```cpp
scope_exit cleanup([] {
  throw std::runtime_error("failure");
});
```

The requirement is deliberate.

The destructor is declared:

```cpp
~scope_exit() noexcept {
  function_();
}
```

A cleanup operation that throws from a destructor is particularly dangerous during exception unwinding. If an exception is already being propagated, a second exception escaping the destructor would result in `std::terminate()`.

Requiring the callable to be nothrow-invocable makes this property explicit at construction time.

## Destruction Order

`scope_exit` follows normal C++ object-lifetime rules.

Multiple guards execute in reverse order of construction:

```cpp
std::string order;

{
  scope_exit first([&order]() noexcept {
    order += '1';
  });

  scope_exit second([&order]() noexcept {
    order += '2';
  });

  scope_exit third([&order]() noexcept {
    order += '3';
  });
}
```

The resulting order is:

```text
321
```

This is the same deterministic destruction ordering provided by ordinary automatic objects.

## Ownership of the Callable

The callable is stored by value inside the `scope_exit` object:

```cpp
F function_;
```

Construction moves the callable into the guard:

```cpp
explicit scope_exit(F&& function) noexcept
    : function_(std::move(function)) {}
```

This means the guard owns the callable for the lifetime of the scope.

## Non-Movable and Non-Copyable

`scope_exit` objects cannot be copied or moved:

```cpp
scope_exit(const scope_exit&) = delete;
scope_exit& operator=(const scope_exit&) = delete;

scope_exit(scope_exit&&) = delete;
scope_exit& operator=(scope_exit&&) = delete;
```

This keeps the lifetime relationship straightforward:

```text
scope_exit lifetime
       |
       +-- callable lifetime
       |
       +-- callback executes exactly once
                         |
                         v
                  object destruction
```

A guard represents an action associated with one particular scope. Moving or copying that responsibility is therefore intentionally not supported.

## Design Philosophy

`scope_exit` is modeled after `std::experimental::scope_exit`. It is not intended to be a general-purpose resource-management framework.

Its responsibility is deliberately narrow:

> Execute one nothrow callable when leaving a scope.

The small implementation also makes its lifetime semantics easy to inspect and reason about.

## Non-Goals

`scope_exit` is not intended to be:

* A general-purpose smart pointer.
* A replacement for RAII resource-owning types.
* A resource-management framework.
* A mechanism for handling exceptions.
* A container for multiple cleanup operations.
* A callback scheduler.
* A substitute for proper ownership types.

When a resource has well-defined ownership semantics, a dedicated RAII type is generally preferable.

`scope_exit` is most useful when the cleanup action is local to a scope and does not justify introducing another resource-owning abstraction.

## Requirements

The implementation requires:

* C++23 or later.
* A standard library providing the required C++ type traits and utility facilities.

The implementation itself has no external dependencies.

## Example

A typical use is to perform cleanup while keeping the main operation linear:

```cpp
void Process() {
  Prepare();

  scope_exit cleanup([]() noexcept {
    Cleanup();
  });

  PerformOperation();

  if (!Validate()) {
    return;
  }

  Commit();
}
```

The cleanup operation is declared once, near the point where the responsibility begins, and is automatically performed regardless of whether the function:

* Reaches the end normally.
* Returns early.
* Exits because of an exception.

## Repository Role

`scope_exit` should remain a small utility.

Its value is not in providing a large abstraction. Its value is in making a common lifetime pattern explicit, deterministic, and difficult to accidentally omit.

Changes to the implementation should therefore be evaluated against a simple criterion:

> Does the change improve the clarity or correctness of scope-exit cleanup without making the utility more complicated than the problem it solves?
