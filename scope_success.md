# scope_success

`scope_success` is a small C++23 RAII utility that executes a callable only if the enclosing scope is exited normally, i.e., no exception is propagating through it at the time. If an exception is unwinding through the scope, the callable never runs.

## Purpose

Some actions should only happen once a block of work has actually succeeded:

* Marking a cache entry or resource as ready for use.
* Sending a completion notification.
* Incrementing a "succeeded" counter or metric.
* Committing a change that should only be visible after everything preceding it worked.

Using `scope_exit` for this would run the action unconditionally, including on the failure path, which usually isn't wanted. `scope_success` fires only when the surrounding scope is left without an exception in flight.

The implementation is intentionally small. It provides one core responsibility: **invoke a callable exactly once, when the `scope_success` object is destroyed while no exception is unwinding through its scope**, with move transfer of that responsibility as a secondary capability.

## Basic Usage

```cpp
#include "scope.hpp"

void Function(Cache& cache, const Key& key) {
  scope_success mark_ready([&cache, key]() noexcept {
    cache.MarkReady(key);
  });

  PopulateEntry(cache, key);
  FinalizeEntry(cache, key);
}
```

If either call throws, `mark_ready`'s destructor runs while an exception is unwinding and does nothing. If both calls succeed and the function reaches its end normally, the destructor observes no exception in flight and marks the entry ready.

## Runs Only on Success

```cpp
void Function() {
  scope_success on_success([]() noexcept {
    Finalize();
  });

  // No exception thrown; the scope exits normally.
}
```

`Finalize()` runs here, since the scope exited without an exception unwinding through it.

## Does Not Run on Failure

```cpp
void Function() {
  scope_success on_success([]() noexcept {
    Finalize();
  });

  throw std::runtime_error("failure");
}
```

`Finalize()` never runs. This is the mirror image of `scope_fail`'s behavior, and the opposite of `scope_exit`'s unconditional behavior.

## Detecting Success

Like `scope_fail`, `scope_success` records how many exceptions are currently unwinding at construction time and compares that snapshot against the same count at destruction time - the comparison is simply inverted:

```cpp
int exception_count_;
```

```cpp
~scope_success() noexcept {
  if (active_ && std::uncaught_exceptions() <= exception_count_) {
    function_();
  }
}
```

If the count at destruction is no greater than the snapshot, nothing new has started unwinding through this scope since construction, and the callable runs. If the count has risen, the destructor is a no-op.

## `noexcept` Requirement

The destructor is `noexcept`, for the same reason as the other two guards: a throwing action during destruction risks `std::terminate()` if it happens while unwinding. There is exactly one class-level invariant enforcing nothrow invocability, checked unconditionally for every instantiation of `scope_success<F>`:

```cpp
static_assert(std::is_nothrow_invocable_v<F&>);
```

This requirement is deliberately stricter than the standard proposal. `std::experimental::scope_success` only requires the exit function to be `Destructible` and invocable - a throwing exit function there can never actually collide with an in-flight exception, since `scope_success`'s action only ever runs on the non-unwinding path. This implementation enforces the nonthrowing requirement anyway, purely so all three guards (`scope_exit`, `scope_fail`, `scope_success`) share one uniform, unconditionally-`noexcept` contract, rather than two different rules depending on which guard is in use.

Unlike `scope_exit` and `scope_fail`, neither constructor here carries an additional `requires std::is_nothrow_invocable_v<const F&>` constraint. That constraint exists on the other two guards only to support the compensating callback in their constructors' `catch` blocks (see "Ownership and Construction" below); since `scope_success`'s constructors have no such `catch` block, nothing requires the callable to be invocable through a `const F&` specifically.

## Destruction Order

Like the other two guards, multiple `scope_success` guards run in reverse order of construction, following ordinary object-lifetime rules - though only the guards whose destructor actually observes no unwinding exception will invoke their callable.

## Ownership and Construction

The callable and the exception-count snapshot are stored by value inside the object:

```cpp
F function_;
int exception_count_;
```

Unlike `scope_exit` and `scope_fail`, construction does **not** use a function-try-block:

```cpp
explicit scope_success(F&& function) noexcept(
    std::is_nothrow_move_constructible_v<F>)
    : function_(std::move(function)),
      exception_count_(std::uncaught_exceptions()) {}
```

If constructing the stored callable throws here, that is a construction failure, not a successful scope exit - the success action must *not* run in that case. A plain member-initializer list is sufficient; there is no compensating catch to write, because there is nothing to compensate for.

## Releasing a Guard

A guard can be disarmed so it no longer invokes its callable on destruction:

```cpp
void release() noexcept { active_ = false; }
```

Useful when a caller decides partway through that "success" shouldn't trigger the finalization action after all, even though no exception will be thrown.

## Move Semantics

Moving a `scope_success` transfers the exit function, the exception-count snapshot, and the active state to the destination, and disarms the source:

```cpp
scope_success(scope_success&& other) noexcept(
    std::is_nothrow_move_constructible_v<F>)
  requires std::is_move_constructible_v<F>
    : function_(std::move(other.function_)),
      exception_count_(other.exception_count_),
      active_(other.active_) {
  other.release();
}
```

The `requires` clause exists for diagnostics, exactly as it does for `scope_exit` and `scope_fail`: without it, moving a `scope_success<F>` whose `F` isn't move-constructible would fail deep inside the initializer list; with it, overload resolution fails earlier, at the actual constraint that wasn't satisfied.

Move assignment is disabled for the same reason as the other two guards:

```cpp
scope_success& operator=(scope_success&&) = delete;
```

## Non-Copyable

```cpp
scope_success(const scope_success&) = delete;
scope_success& operator=(const scope_success&) = delete;
```

Copying would give two guards the same pending action and the same exception-count snapshot, causing the success action to potentially run twice for what was really one success.

## Deduction Guide

```cpp
template <typename F>
scope_success(F) -> scope_success<std::decay_t<F>>;
```

Without this, constructing a `scope_success` from an lvalue callable would deduce a reference-type template parameter, and the object would store a reference member instead of a value. The explicit guide forces decay to a value type regardless of how the argument is passed.

## Design Philosophy

`scope_success` is modeled after `std::experimental::scope_success`, with one deliberate divergence: this implementation requires a nonthrowing callable, where the standard facility does not. Its responsibility is otherwise deliberately narrow:

> Execute one nothrow-invocable callable exactly once, only if the scope is left without an exception unwinding through it, unless explicitly released beforehand.

## Non-Goals

`scope_success` is not intended to be:

* A general-purpose completion-notification framework.
* A substitute for a return value or an explicit "did this succeed" signal the caller actually needs to inspect.
* A mechanism for running code unconditionally - use `scope_exit` for that.
* A mechanism for running code only on failure - use `scope_fail` for that.

## Requirements

The implementation requires:

* C++23 or later.
* A standard library providing the required C++ type traits and utility facilities.

The implementation itself has no external dependencies.

## Example

```cpp
void PopulateCache(Cache& cache, const Key& key) {
  scope_success mark_ready([&cache, key]() noexcept {
    cache.MarkReady(key);
  });

  PopulateEntry(cache, key);
  FinalizeEntry(cache, key);
}
```

If either call throws, the entry is left unmarked. If both succeed, `mark_ready`'s destructor finds no exception in flight and marks the entry ready - exactly once, and only once the work above is actually done.

## Repository Role

`scope_success` should remain a small utility, matching `scope_exit` and `scope_fail` in scope and implementation style.

Changes to the implementation should therefore be evaluated against a simple criterion:

> Does the change improve the clarity or correctness of success-only finalization without making the utility more complicated than the problem it solves?
