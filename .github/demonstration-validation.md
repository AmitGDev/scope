`src\main.cpp` should continue to demonstrate:

1. Normal scope exit - the guard's callback runs when a scope is left
   normally.
2. Early return - the callback runs even when the enclosing function returns
   before reaching the end of its scope.
3. Exception unwinding - the callback runs while an exception propagates
   through the guard's scope.
4. Multiple guards - several guards in the same scope run their callbacks in
   reverse declaration order.
5. Callable ownership - a callable can be moved into the guard rather than
   copied.

Keep the demonstration deterministic and avoid timing-dependent or environment-dependent behavior.
