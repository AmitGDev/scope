# Why `run-clang-tidy.py` is here

This document explains what `run-clang-tidy.py` is and why this repository vendors it.

## Background: LLVM, clang-tidy, and `run-clang-tidy.py`

`run-clang-tidy.py` is an official Python helper script shipped with the **clang-tools-extra** part of the LLVM source tree.

In the LLVM source tree, it is located under a path such as:

```text
llvm/tools/clang/tools/extra/clang-tidy/tool/run-clang-tidy.py
```

LLVM documentation describes it as a way to run clang-tidy in parallel over a `compile_commands.json` database.

However, **prebuilt LLVM/Clang distributions do not always install this script in a location convenient for use**, even when they provide the `clang-tidy` executable.

For example:

* Some Linux distributions split LLVM tools into separate packages; the script may be provided by an additional package or not installed by default.
* Windows LLVM installations and minimal builds may provide the main executables without placing the Python helper scripts in a convenient location or on `PATH`.

Relying on a system-installed copy therefore makes the availability and location of `run-clang-tidy.py` dependent on the particular LLVM distribution and installation.

As a result, scripts and CI workflows that assume a system-installed `run-clang-tidy.py` may not work consistently across environments.

## 1. Why we vendor the script

The repository vendors `run-clang-tidy.py` to ensure a known and consistent clang-tidy runner is available across development and CI environments.

This avoids depending on the exact packaging and installation layout of the LLVM distribution. It also gives the project control over the runner version and its invocation, including parallel execution (`-j`).

## 2. Why this project invokes it differently on each OS

The project uses Ninja to generate `compile_commands.json`. Although Ninja is used on both platforms, the compilation commands recorded in the database use different compiler drivers:

* **Windows:** Ninja uses **MSVC** (`cl.exe`) flags. When `run-clang-tidy.py` invokes `clang-tidy`, we pass `--driver-mode=cl` so clang-tidy interprets those MSVC-style arguments correctly.
* **Linux:** Ninja uses **Clang** (`clang++`) flags, which clang-tidy understands natively. The MSVC driver mode must not be used because it can cause incorrect compiler and system-header interpretation, including failures to locate headers such as `cstdint`.

Therefore, when this project's `run-clang-tidy.py` is invoked:

* Windows: use `--driver-mode=cl`
* Linux: do not use `--driver-mode=cl`

## 3. Maintenance

When updating the vendored `run-clang-tidy.py`, preserve its compatibility with the clang-tidy version used by the project.

If the project changes its compiler, C++ standard, or other compilation settings, verify that the invocation of `run-clang-tidy.py` remains compatible with the resulting `compile_commands.json`.

The platform-specific invocation is implemented by the project's clang-tidy action rather than by modifying the vendored script for each operating system.
