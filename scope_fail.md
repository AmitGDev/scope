# scope_fail

`scope_fail` is a small C++23 RAII utility that executes a callable only if the enclosing scope is exited because an exception is propagating through it. If the scope exits normally, the callable never runs.

## Purpose

Some cleanup only makes sense on the failure path:

* Rolling back a partial transaction.
* Undoing a partial state change.
* Compensating for a side effect that only matters if something afterward failed.
* Logging or reporting a diagnostic specific to the failure path.

Using `scope_exit` for this would run the action unconditionally, including on success, where it is often not just unnecessary but wrong (rolling back work that should be kept). `scope_fail` fires only when the surrounding scope is left through an exception.

The implementation is intentionally small. It provides one core responsibility: **invoke a callable exactly once, when the `scope_fail` object is destroyed while an exception is unwinding through its scope**, with move transfer of that responsibility as a secondary capability.

## Basic Usage

```cpp
#include "scope.hpp"

void Function() {
  Transaction tx = BeginTransaction();

  scope_fail rollback([&tx]() noexcept {
    tx.Rollback();
  });

  ApplyChange(tx, first_change);
  ApplyChange(tx, second_change);

  tx.Commit();
}
```

If `ApplyChange` throws, `rollback`'s destructor runs while the exception is unwinding and rolls the transaction back. If both calls succeed and `tx.Commit()` is reached, the scope still exits normally afterward, and `rollback`'s destructor finds no exception in flight - it does nothing.

## Runs Only on the Failure Path

```cpp
void Function() {
  scope_fail on_failure([]() noexcept {
    Cleanup();
  });

  throw std::runtime_error("failure");
}
```

Because the scope is exited via an exception, `Cleanup()` runs.

## Does Not Run on Success

```cpp
void Function() {
  scope_fail on_failure([]() noexcept {
    Cleanup();
  });

  // No exception thrown; the scope exits normally.
}
```

Here `Cleanup()` never runs. This is the behavioral difference from `scope_exit`: the action is conditional on *how* the scope was left, not merely on the object's destruction.

## Detecting Failure

`scope_fail` cannot wrap the caller's code in a `try`/`catch`; it has no way to reach into the scope that constructed it. Instead, it records how many exceptions are currently unwinding at construction time, and compares that snapshot against the same count at destruction time:

```cpp
int exception_count_;
```

```cpp
~scope_fail() noexcept {
  if (active_ && std::uncaught_exceptions() > exception_count_) {
    function_();
  }
}
```

If the count at destruction is greater than the snapshot, a new exception has started unwinding through this scope since the guard was constructed, and the callable runs. If the count is unchanged, the scope exited normally, and the destructor is a no-op.

## `noexcept` Requirement

The destructor is declared `noexcept`, for the same reason as `scope_exit`'s: a cleanup action that throws while another exception is already unwinding would cause `std::terminate()`. There is exactly one class-level invariant enforcing nothrow invocability, checked unconditionally for every instantiation of `scope_fail<F>`:

```cpp
static_assert(std::is_nothrow_invocable_v<F&>);
```

This single assert backs the destructor's call above, and the recovery call used by both constructors' failure paths (see "Ownership and Construction" below).

The const-lvalue constructor additionally requires the callable to be invocable through a `const F&`, but - exactly as in `scope_exit` - this is expressed as a `requires` clause on that specific constructor, not a second `static_assert`:

```cpp
explicit scope_fail(const F& function) noexcept(
    std::is_nothrow_copy_constructible_v<F>)
  requires std::is_nothrow_invocable_v<const F&>
try : function_(function), exception_count_(std::uncaught_exceptions()) {
} catch (...) {
  function();
  throw;
}
```

A mutable lambda (non-const `operator()`) simply isn't invocable through `const F&`, so this overload doesn't participate in overload resolution for it. The object can still be constructed through the `F&&` overload instead, which carries no such constraint.

## Destruction Order

Like `scope_exit`, multiple `scope_fail` guards run in reverse order of construction, following ordinary object-lifetime rules - though only the guards whose destructor actually observes an unwinding exception will invoke their callable.

## Ownership and Construction

The callable and the exception-count snapshot are stored by value inside the object:

```cpp
F function_;
int exception_count_;
```

As with `scope_exit`, constructing the stored callable can itself throw. If it does, each constructor's failure path invokes the source callable once before propagating the exception - a `scope_fail` that fails to construct is, itself, a failure, so the fail action still needs a chance to run:

```cpp
explicit scope_fail(F&& function) noexcept(
    std::is_nothrow_move_constructible_v<F>) try
    : function_(std::move(function)),
      exception_count_(std::uncaught_exceptions()) {
} catch (...) {
  function();
  throw;
}
```

## Releasing a Guard

A guard can be disarmed so it no longer invokes its callable on destruction:

```cpp
void release() noexcept { active_ = false; }
```

Useful when ownership of the pending rollback is being handed off elsewhere, or when the rollback should not run despite an exception (for example, if the caller intends to retry and has already decided to keep the partial state).

## Move Semantics

Moving a `scope_fail` transfers the exit function, the exception-count snapshot, and the active state to the destination, and disarms the source, so the callback still fires (or doesn't) exactly according to the destination's own outcome:

```cpp
scope_fail(scope_fail&& other) noexcept(
    std::is_nothrow_move_constructible_v<F>)
  requires std::is_move_constructible_v<F>
    : function_(std::move(other.function_)),
      exception_count_(other.exception_count_),
      active_(other.active_) {
  other.release();
}
```

The `requires` clause exists for diagnostics, exactly as in `scope_exit`: without it, moving a `scope_fail<F>` whose `F` isn't move-constructible would fail deep inside the initializer list; with it, overload resolution fails earlier, at the actual constraint that wasn't satisfied.

Move assignment is disabled, for the same reason as `scope_exit`'s: it would require deciding what happens to the destination's own pending action, and no single answer is clearly correct.

```cpp
scope_fail& operator=(scope_fail&&) = delete;
```

## Non-Copyable

```cpp
scope_fail(const scope_fail&) = delete;
scope_fail& operator=(const scope_fail&) = delete;
```

Copying would give two guards the same pending action and the same exception-count snapshot, causing the rollback to potentially run twice for what was really one failure.

## Deduction Guide

```cpp
template <typename F>
scope_fail(F) -> scope_fail<std::decay_t<F>>;
```

Without this, constructing a `scope_fail` from an lvalue callable would deduce a reference-type template parameter under class template argument deduction, and the object would store a reference member instead of a value - reintroducing a dangling-reference risk. The explicit guide forces decay to a value type regardless of how the argument is passed.

## Design Philosophy

`scope_fail` is modeled after `std::experimental::scope_fail`. Its responsibility is deliberately narrow:

> Execute one nothrow-invocable callable exactly once, only if the scope is left while an exception is unwinding through it, unless explicitly released beforehand.

## Non-Goals

`scope_fail` is not intended to be:

* A general-purpose exception-handling mechanism.
* A replacement for a `try`/`catch` block where the caller actually needs to inspect or handle the exception.
* A substitute for transactional types with well-defined commit/rollback semantics.
* A mechanism for running code unconditionally - use `scope_exit` for that.

## Requirements

The implementation requires:

* C++23 or later.
* A standard library providing the required C++ type traits and utility facilities.

The implementation itself has no external dependencies.

## Example

```cpp
void ApplyBatch(Transaction& tx, const std::vector<Change>& changes) {
  scope_fail rollback([&tx]() noexcept { tx.Rollback(); });

  for (const auto& change : changes) {
    ApplyChange(tx, change);
  }

  tx.Commit();
}
```

If any `ApplyChange` call throws, `rollback` undoes the transaction on the way out. If the loop completes and `tx.Commit()` runs, the guard's destructor observes no unwinding exception and does nothing.

## Repository Role

`scope_fail` should remain a small utility, matching `scope_exit` and `scope_success` in scope and implementation style.

Changes to the implementation should therefore be evaluated against a simple criterion:

> Does the change improve the clarity or correctness of failure-path cleanup without making the utility more complicated than the problem it solves?
