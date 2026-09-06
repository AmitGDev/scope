# scope

Three small, dependency-free C++23 scope guards: [`scope_exit`](scope_exit.md), [`scope_fail`](scope_fail.md), and [`scope_success`](scope_success.md). Each ties a callable to how a scope is exited, instead of duplicating that action by hand across every return path.

```cpp
#include "scope.hpp"
```

That's it - a single header, no external dependency.

## Why this exists

* **The proposal never shipped.** `scope_exit`, `scope_fail`, `scope_success`, and `unique_resource` were proposed for Library Fundamentals (P0052) and exist today only as `std::experimental` on the handful of standard libraries that implement the TS at all. The proposal stalling didn't make the underlying need go away.
* **No Boost dependency.** Boost.Scope (or Boost as a whole) is a much heavier dependency than three small class templates justify. This header deliberately mirrors the `std::experimental` names and interface, so swapping to the real standard facility later - if and when one ships - is close to a drop-in change rather than a rewrite.

## The three guards

| Class | Runs its action when... | Typical use |
| --- | --- | --- |
| [`scope_exit`](scope_exit.md) | The scope is exited, unconditionally - success or exception | Cleanup that must always happen (closing a handle, releasing a lock-like resource) |
| [`scope_fail`](scope_fail.md) | The scope is exited via an exception | Undoing partial work only on the failure path (rollback, compensating action) |
| [`scope_success`](scope_success.md) | The scope is exited normally, with no exception in flight | Finalizing work only once everything before it has actually succeeded |

## A small demo of each

```cpp
// scope_exit: always runs, success or exception
FILE* f = std::fopen(path, "rb");
auto exit_guard = scope_exit([f]() noexcept { std::fclose(f); });

// scope_fail: runs only if an exception propagates out of the scope
auto fail_guard = scope_fail([&tx]() noexcept { tx.Rollback(); });

// scope_success: runs only if no exception propagates out of the scope
auto success_guard = scope_success([&cache, key]() noexcept { cache.MarkReady(key); });
```

See each class's own page for the full rationale, API, and worked examples.

## Shared design

* **The exit function must be `noexcept`-invocable.** Enforced with a `static_assert`, not just documented, because each destructor is itself `noexcept`.
* **Move-only.** Copying would give two guards the same pending action; move-assignment would need to define what happens to the target's own pending action.
* **An explicit CTAD deduction guide** on every class, so constructing from an lvalue callable never deduces a reference-type template parameter.
* **`scope_fail` and `scope_success` tell success from failure the same way:** by snapshotting `std::uncaught_exceptions()` at construction and comparing it against the current count at destruction. They differ only in which direction of that comparison fires the action.

## Deviation from the proposal: no throwing exit functions

The `std::experimental` proposal only requires the exit function to be `Destructible` and invocable - nothing forces it to be `noexcept`. This implementation closes that door on all three classes: `static_assert(std::is_nothrow_invocable_v<F&>)` rejects a throwing exit function at compile time, regardless of whether an exception happens to be unwinding at the time. The tradeoff is explicit: give up the rare case of a genuinely throwing exit function, in exchange for a guarantee that none of the three guards can ever call `std::terminate()` as a surprise, because every destructor's contract is unconditionally `noexcept` from the outset.

## Differences at a glance

| Aspect | scope_exit | scope_fail | scope_success |
| --- | --- | --- | --- |
| Fires on destruction when... | always (if active) | uncaught-exception count rose since construction | uncaught-exception count did not rise since construction |
| Detection mechanism | none needed | `std::uncaught_exceptions()` snapshot | `std::uncaught_exceptions()` snapshot |
| Constructor uses a function-try-block | Yes | Yes | No - construction failure must not invoke the exit function |
| Corresponding standard facility | `std::experimental::scope_exit` | `std::experimental::scope_fail` | `std::experimental::scope_success` |

## Requirements

* C++23 or later.
* A standard library providing the required C++ type traits and utility facilities.

No external dependencies.

## License

MIT. See the license header in [`scope.hpp`](scope.hpp).
