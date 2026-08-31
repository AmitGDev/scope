# Why run-clang-tidy.py is here

## 1. The LLVM 20 Windows Issue
LLVM 20.1.8 (and newer) removed `run-clang-tidy.py` from the official Windows installer. We vendor this script manually to:
* Enable parallel execution (`-j`) on Windows runners.
* Prevent CI breakage if the upstream file is moved or renamed.

## 2. Why the flags differ between OSs
We use Ninja to generate `compile_commands.json`. Even though Ninja is the build system for both:

* **On Windows:** Ninja uses **MSVC** (`cl.exe`) flags. `clang-tidy` (being Clang-based) doesn't understand `/std:c++latest`. We use `-extra-arg=--driver-mode=cl` to translate these.
* **On Linux:** Ninja uses **Clang** flags. These are natively understood. Adding the Windows driver mode on Linux causes header lookup failures (e.g., `cstdint` not found).

## 3. Maintenance
If you update the project to a new C++ standard, ensure the `$extra` flags in `static-code-analisys.yml` match your CMake settings.