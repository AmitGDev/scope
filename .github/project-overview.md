`scope` is an intentionally small, cross-platform C++23 utility that demonstrates and implements a `scope_exit` scope guard. The project focuses on predictable RAII-based cleanup semantics and serves as a compact, self-contained example of modern C++ resource management.

The implementation is provided by `src/scope.hpp`. The `src/main.cpp` executable demonstrates normal scope exit, early return, exception unwinding, multiple guards, callable ownership, and compile-time properties.

Keep the implementation small and focused. Do not introduce unnecessary abstractions, dependencies, or framework code.