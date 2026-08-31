# Copilot instructions

## Project overview

@project-overview.md

## Build and validation

### Prerequisites

* CMake 3.25 or newer.
* Ninja.
* A C++23-capable compiler.
* On Windows, an x64 MSVC C++ toolchain and `vswhere.exe`.
* On CI, LLVM 23, providing `clang-format` and `clang-tidy`.
* Python when running `.github\scripts\run-clang-tidy.py` directly.

### Configure and build locally

`build-x64.ps1` supports Windows x64 MSVC builds. It accepts `Debug` or
`Release` through `-Configuration` and can remove the build tree first with
`-Clean`. Choose whichever configuration is relevant to the change being
validated; do not default to `Debug` unless nothing indicates otherwise:

```powershell
.\build-x64.ps1
.\build-x64.ps1 -Configuration Release
.\build-x64.ps1 -Clean
.\build-x64.ps1 -Configuration Release -Clean
```

The equivalent CMake presets are:

```powershell
cmake --preset x64-debug
cmake --build --preset x64-debug

cmake --preset x64-release
cmake --build --preset x64-release
```

The presets use Ninja and configure `cl.exe`, writing to
`build\x64-debug` or `build\x64-release`. The resulting executable name and
output path are defined by the target in `CMakePresets.json` /
`CMakeLists.txt`; look there rather than assuming a fixed binary name.
CMake also mirrors the active compilation database to
`build\compile_commands.json` for clangd and clang-tidy.

On Linux, configure and build with the same Ninja workflow using Clang,
substituting `Debug` or `Release` for whichever configuration is relevant to
the change being validated (the example below uses `Debug` for illustration
only, not as a default to always use):

```bash
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++
cmake --build build --config Debug -j
```

### Formatting

CI checks every tracked C++ source and header file using the repository's
`.clang-format` configuration. The style is Google-derived, uses two-space
indentation, and has an 80-column limit.

Check files locally:

```powershell
clang-format --style=file --dry-run --Werror src/*.hpp src/*.cpp
```

Apply formatting:

```powershell
clang-format -i src/*.hpp src/*.cpp
```

The CI action discovers tracked `*.cpp`, `*.h`, `*.hpp`, `*.cc`, and `*.cxx`
files under the whole repository, so new C++ files are automatically
included; no per-file or per-project changes are needed.

### Static analysis

`.clang-tidy` enables broad bug-prone, analyzer, concurrency, core-guidelines,
Google, LLVM, misc, modernize, performance, portability, and readability
checks, plus selected HICPP and CERT checks. Warnings are errors. Keep
`NOLINT` annotations narrowly scoped and use them only for deliberate,
documentable exceptions.

After configuring the project, run the vendored runner:

```powershell
python .github\scripts\run-clang-tidy.py -p build "src/.*"
```

The CI composite action analyzes every `.cpp` file found under `src` (via
wildcard discovery, not a fixed file list) one at a time with
`clang-tidy -p build` and `-warnings-as-errors=*`. The vendored script exists
for reliable parallel execution, including on Windows LLVM packages that omit
`run-clang-tidy.py`.

Ninja is required because the clang-tidy workflow depends on
`compile_commands.json`. On Windows, MSVC compilation commands require Clang's
MSVC driver mode (`--driver-mode=cl`) when using the vendored runner; Linux
Clang commands must not receive that option.

### Demonstration validation

There is no test framework or test target. The demonstration is the
executable built from the file containing `main()` under `src\`.

* **Target and output layout**: read the target name from
  `CMakePresets.json` and `CMakeLists.txt`. Never hardcode a binary name.
* **Run**: launch the executable from the active preset's own output
  directory, e.g. `build\<preset>\bin\<target>.exe` on Windows, or the
  Linux equivalent.
* **Pass criterion**: after any change under `src\`, the demonstration must
  run to completion and report success.
* **Expected behavior**: if `demonstration-validation.md` is present, use
  its description. Otherwise read `main()` directly.
* **On mismatch**: if `main()` no longer matches
  `demonstration-validation.md`, report the discrepancy rather than editing
  either file. A human decides whether the description is stale or the code
  regressed.

## Continuous integration

`.github\workflows\static-code-analysis.yml` runs on pushes, pull requests,
weekly on Sundays at 06:00 UTC, and manual dispatch. Its matrix covers:

* `windows-latest` and `ubuntu-latest`.
* `Debug` and `Release`.

Each matrix job checks out the repository, installs the toolchain, checks
formatting, optionally runs CodeQL for C++, configures and builds with Ninja,
verifies `build/compile_commands.json`, runs clang-tidy, and optionally
performs CodeQL analysis. CodeQL is enabled by default through
`CODEQL_ENABLED: 1`; set it to `0` in the workflow to disable it. Failed jobs
upload logs for seven days, and every job uploads its compilation database.

The workflow delegates implementation details to these reusable composite
actions:

* `.github\actions\setup-cpp-tools`: MSVC development environment on Windows;
  installs `clang-format`/`clang-tidy` and Ninja on both platforms. Windows
  installs an exact, hardcoded LLVM release (`LLVM_VERSION` in the action).
  Linux installs the latest LLVM 23.x packages from apt.llvm.org, which apt
  only ever serves as the current latest patch for a major version. The two
  are therefore not automatically kept in sync: when apt.llvm.org rolls
  Linux forward to a newer patch, bump the Windows `LLVM_VERSION` to match.
* `.github\actions\check-formatting`: checks all tracked C++ files
  (wildcard discovery; no project-specific file list).
* `.github\actions\configure-cmake`: configures Ninja builds with MSVC on
  Windows and Clang on Linux.
* `.github\actions\build-project`: builds the selected Debug or Release
  configuration in parallel.
* `.github\actions\run-clang-tidy`: validates the compilation database and
  analyzes source files under `src` (wildcard discovery).

Keep the workflow platform-agnostic. Put platform-specific shell commands in
the relevant composite action rather than adding conditionals to the main
workflow. Update `.github\WORKFLOWS.md` when changing the CI architecture,
inputs, tool versions, or troubleshooting guidance.

## Repository conventions

* Use clear, idiomatic modern C++ and preserve portability between Windows
  and Linux; every source file under `src\` must build and run unmodified
  on both platforms. Keep platform-specific code isolated and guarded
  rather than spread across shared headers.
* Follow the existing clang-tidy naming rules: free functions and methods use
  `CamelCase`, locals and parameters use `lower_case`, `constexpr` variables use
  `kCamelCase`, and namespaces use `lower_case`.
* Follow Google C++ style and `.clang-format`; use `static` rather than
  anonymous namespaces for internal free functions or objects when internal
  linkage is needed.
* Avoid unrelated behavior, third-party dependencies, and speculative
  compatibility layers.

## Important files

* `src\*.hpp`, `src\*.cpp`: implementation and demonstration/entry-point
  sources (see Project overview for which files these are in this project).
* `README.md`: library features, usage, API, and requirements.
* `CMakeLists.txt`: C++23 target, compiler options, output, install rule, and
  compilation-database synchronization.
* `CMakePresets.json`: Windows x64 Debug and Release Ninja presets; also the
  source of truth for the built executable's target name.
* `build-x64.ps1`: Windows MSVC environment setup and preset build wrapper.
* `.github\workflows\static-code-analysis.yml`: cross-platform CI matrix,
  CodeQL, artifacts, and orchestration.
* `.github\WORKFLOWS.md`: CI architecture and maintenance documentation.
* `.github\actions\`: reusable setup, formatting, CMake, build, and clang-tidy
  composite actions.
* `.github\scripts\run-clang-tidy.py`: parallel clang-tidy runner.
* `.github\scripts\RUN_CLANG_TIDY_README.md`: runner platform and flag rationale.
* `.clang-format`: formatting policy.
* `.clang-tidy`: static-analysis checks and naming policy.
* `.clangd`: points clangd at `build\compile_commands.json`.
