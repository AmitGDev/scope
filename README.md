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

The implementation is intentionally small. It provides one core responsibility: **invoke a callable when the `scope_exit` object is destroyed**, with move transfer of that responsibility as a secondary capability.

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

The destructor is declared:

```cpp
~scope_exit() noexcept {
  if (active_) {
    function_();
  }
}
```

A cleanup operation that throws from a destructor is particularly dangerous during exception unwinding. If an exception is already being propagated, a second exception escaping the destructor would result in `std::terminate()`.

To make this safe, the callable must be nothrow-invocable:

```cpp
static_assert(std::is_nothrow_invocable_v<F&>);
static_assert(std::is_nothrow_invocable_v<const F&>);
```

These two asserts cover different call sites, not the same requirement twice:

* `is_nothrow_invocable_v<F&>` is an unconditional class invariant. It backs the destructor's call above, and also backs the recovery call used by the `F&&` constructor's failure path (see "Ownership and Construction" below).
* `is_nothrow_invocable_v<const F&>` is required specifically because the lvalue-copy constructor's failure path invokes the callable through a `const F&`. A mutable lambda (non-const `operator()`) will fail this assert and can only be used via the `F&&` overload instead.

Given these constraints, this is valid:

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

Requiring the callable to be nothrow-invocable makes the destructor's safety explicit at construction time, rather than leaving it to be discovered at the moment of unwinding.

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

## Ownership and Construction

The callable is stored by value inside the `scope_exit` object:

```cpp
F function_;
```

`scope_exit` can be constructed from either an lvalue or an rvalue callable:

```cpp
explicit scope_exit(const F& function) noexcept(
    std::is_nothrow_copy_constructible_v<F>);

explicit scope_exit(F&& function) noexcept(
    std::is_nothrow_move_constructible_v<F>);
```

Construction is only unconditionally `noexcept` when `F`'s copy or move constructor is itself `noexcept`. If constructing `function_` throws, each constructor's failure path invokes the source callable once before propagating the exception, so a resource the callable is responsible for still gets a chance to be cleaned up:

```cpp
explicit scope_exit(F&& function) noexcept(
    std::is_nothrow_move_constructible_v<F>) try
    : function_(std::move(function)) {
} catch (...) {
  function();
  throw;
}
```

This recovery is best-effort: the state of `function` after a failed copy or move depends on whatever exception guarantee `F`'s constructor provides. At minimum it remains destructible, but its value and behavior are otherwise unspecified. Nothrow invocability only guarantees the recovery call itself won't throw, not that it retains its original semantics.

## Releasing a Guard

A guard can be disarmed so it no longer invokes its callable on destruction:

```cpp
void release() noexcept { active_ = false; }
```

```cpp
void Function() {
  scope_exit cleanup([]() noexcept {
    Cleanup();
  });

  // ...operation succeeds...

  cleanup.release();  // Cleanup() will not run.
}
```

This is useful when the cleanup should only fire on the failure path of an operation that otherwise completes normally, or when ownership of the pending action is being handed off elsewhere (see "Move Semantics" below, where `release()` is used internally to disarm a moved-from guard).

## Move Semantics

Unlike copying, moving a `scope_exit` is supported. Moving transfers responsibility for invoking the callable to the destination guard and disarms the source, so the callback still executes exactly once:

```cpp
scope_exit(scope_exit&& other) noexcept(
    std::is_nothrow_move_constructible_v<F>)
    : function_(std::move(other.function_)), active_(other.active_) {
  other.release();
}
```

This allows a guard to be returned by value from a factory function or relocated into a container:

```cpp
scope_exit<CleanupFn> MakeCleanupGuard(Resource& resource) {
  return scope_exit([&resource]() noexcept {
    ReleaseResource(resource);
  });
}

void Function() {
  auto guard = MakeCleanupGuard(resource);
  // guard now owns the cleanup action; resource is released
  // when guard goes out of scope here, not inside MakeCleanupGuard.
}
```

Move *assignment*, however, is disabled:

```cpp
scope_exit& operator=(scope_exit&&) = delete;
```

Move assignment would require deciding how to handle the destination's existing, still-pending cleanup action (run it immediately, discard it, or something else), and no single choice is clearly correct for all callers. Rather than pick a policy, move assignment is simply not provided.

## Non-Copyable

`scope_exit` objects cannot be copied:

```cpp
scope_exit(const scope_exit&) = delete;
scope_exit& operator=(const scope_exit&) = delete;
```

```text
scope_exit lifetime
       |
       +-- callable lifetime
       |
       +-- callback executes exactly once
                         |
                         v
                  object destruction (or transfer via move)
```

A guard represents an action associated with one particular scope. Copying would give two guards ownership of the same cleanup action, causing it to run more than once, so copying is disabled. Moving avoids this problem by transferring ownership rather than duplicating it, which is why it's permitted where copying is not.

## Design Philosophy

`scope_exit` is modeled after `std::experimental::scope_exit`. It is not intended to be a general-purpose resource-management framework.

Its responsibility is deliberately narrow:

> Execute one nothrow-invocable callable exactly once, either when leaving a scope or when explicitly released beforehand.

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

  cleanup.release();
  Commit();
}
```

The cleanup operation is declared once, near the point where the responsibility begins, and is automatically performed unless explicitly released, regardless of whether the function:

* Reaches the end normally.
* Returns early.
* Exits because of an exception.

## Repository Role

`scope_exit` should remain a small utility.

Its value is not in providing a large abstraction. Its value is in making a common lifetime pattern explicit, deterministic, and difficult to accidentally omit.

Changes to the implementation should therefore be evaluated against a simple criterion:

> Does the change improve the clarity or correctness of scope-exit cleanup without making the utility more complicated than the problem it solves?