# GitHub CI Infrastructure

This directory contains the GitHub Actions workflows and reusable composite actions that implement the project's CI/CD pipeline.

## Architecture

The CI/CD infrastructure follows a **Recipe Pattern**:

* **Workflows** are recipes that define high-level, platform-agnostic orchestration.
* **Composite Actions** are ingredients that encapsulate reusable implementation details.
* **Configuration** is independent from the workflow and controls the analysis matrix and optional features.

This separation keeps workflows focused on orchestration while allowing implementation details and analysis parameters to evolve independently.

### Overall Flow

```text
Configuration
     |
     v
Process & Validate Configuration
     |
     v
Create Analysis Matrix
     |
     v
Analyze Each Matrix Entry
     |
     +-- Checkout
     +-- C++ Dependencies (e.g. spdlog, Boost)
     +-- C++ Environment (e.g. MSVC, LLVM, Ninja)
     +-- clang-format
     +-- CMake Configure
     +-- CodeQL Init [optional]
     +-- Build
     +-- Verify compile_commands.json
     +-- clang-tidy
     +-- CodeQL Analysis [optional]
     +-- Artifacts
```

---

## Repository Structure

```text
.github/
├── actions/
│   ├── process-analysis-config/
│   │   └── action.yml
│   ├── setup-cpp-dependencies/
│   │   └── action.yml
│   ├── setup-cpp-environment/
│   │   └── action.yml
│   ├── check-formatting/
│   │   └── action.yml
│   ├── configure-cmake/
│   │   └── action.yml
│   ├── build-project/
│   │   └── action.yml
│   └── run-clang-tidy/
│       └── action.yml
├── scripts/
│   ├── run-clang-tidy.py
│   └── RUN_CLANG_TIDY_README.md
├── workflows/
│   └── static-code-analysis.yml
├── static-code-analysis.json
└── CI.md
```

---

## Configuration

The analysis pipeline is controlled by:

```text
.github/static-code-analysis.json
```

The configuration defines the analysis matrix and optional analysis features without requiring changes to the workflow itself.

### Configuration Example

```json
{
  "os": [
    "windows-latest",
    "ubuntu-latest"
  ],
  "configurations": [
    "Debug",
    "Release"
  ],
  "codeql": true
}
```

### Configuration Properties

| Property         | Type    | Description                                        |
| ---------------- | ------- | -------------------------------------------------- |
| `os`             | array   | GitHub Actions runner operating systems to analyze |
| `configurations` | array   | CMake build configurations to analyze              |
| `codeql`         | boolean | Enables or disables CodeQL analysis                |

The `os` and `configurations` arrays form a Cartesian-product matrix. For example, two operating systems and two configurations produce four analysis jobs:

```text
Windows + Debug
Windows + Release
Linux   + Debug
Linux   + Release
```

### Configuration Validation

Configuration is processed before the analysis matrix is created.

The configuration processor:

* Loads `.github/static-code-analysis.json`.
* Validates that required properties are present.
* Validates the supported operating-system values.
* Validates the build configurations.
* Validates that `codeql` is a boolean.
* Rejects empty `os` or `configurations` arrays.
* Produces normalized workflow outputs.

The configuration has **no implicit defaults**. Invalid or incomplete configuration causes the workflow to fail rather than silently selecting fallback values.

### CodeQL Configuration

CodeQL is controlled exclusively by the `codeql` property.

Enable CodeQL:

```json
{
  "os": [
    "windows-latest",
    "ubuntu-latest"
  ],
  "configurations": [
    "Debug",
    "Release"
  ],
  "codeql": true
}
```

Disable CodeQL:

```json
{
  "os": [
    "windows-latest",
    "ubuntu-latest"
  ],
  "configurations": [
    "Debug",
    "Release"
  ],
  "codeql": false
}
```

All other analysis steps remain enabled regardless of the CodeQL setting.

---

## Workflow

### Static Code Analysis Recipe

The main workflow is:

```text
.github/workflows/static-code-analysis.yml
```

Its purpose is to perform cross-platform static analysis of the C++23 project using formatting checks, clang-tidy, and optionally CodeQL.

### Triggers

The workflow can run from:

* Pushes to branches.
* Pull requests.
* The scheduled weekly run.
* Manual `workflow_dispatch`.

### Trigger Policy

The workflow runs for pushes and pull requests on all branches, except when all changed files match the ignore policy.

The ignore policy is defined once and reused by both `push` and `pull_request`:

```yaml
on:
  push:
    branches: ["*"]
    paths-ignore: &ignore_policy
      - '**/*.md'
      - 'docs/**'
      - 'LICENSE'
      - '.gitignore'

  pull_request:
    branches: ["*"]
    paths-ignore: *ignore_policy
```

The ignored paths are:

| Pattern      | Purpose                      |
| ------------ | ---------------------------- |
| `**/*.md`    | Markdown documentation files |
| `docs/**`    | Documentation directory      |
| `LICENSE`    | License file                 |
| `.gitignore` | Git ignore configuration     |

CI is skipped only when **all changed files** match the ignore policy.

For example:

* A change to `README.md` only → **CI does not run**
* A change under `docs/` only → **CI does not run**
* A change to `README.md` and `src/main.cpp` → **CI runs**
* A change to `.github/static-code-analysis.json` → **CI runs**
* A change to `.clang-format` or `.clang-tidy` → **CI runs**
* A change to `CMakeLists.txt` → **CI runs**

The policy deliberately uses an **ignore list** rather than a whitelist. This keeps new source, build, configuration, and policy files automatically eligible for CI without requiring the trigger configuration to be updated whenever the repository structure grows.

### Workflow Structure

The workflow contains two jobs:

```text
Process analysis configuration
             |
             v
        Analyze matrix
             |
       +-----+-----+
       |     |     |
       v     v     v
     Matrix entries
```

The first job processes and validates the configuration.

The second job uses the resulting values to create and execute the analysis matrix.

---

### Job 1: Process Analysis Configuration

**Job ID:**

```text
process-analysis-config
```

**Display name:**

```text
Process analysis configuration
```

This job runs on `ubuntu-latest`.

It performs a sparse checkout containing only:

```text
.github/actions/process-analysis-config
.github/static-code-analysis.json
```

It then executes the `process-analysis-config` composite action.

The job exposes the processed configuration as workflow outputs:

```text
os
configurations
codeql
```

These outputs are consumed by the analysis job.

Keeping configuration processing in a separate job prevents configuration parsing and validation from being duplicated across matrix jobs.

---

### Job 2: Analyze

**Job ID:**

```text
analyze
```

The analysis job depends on the configuration-processing job and creates its matrix from the validated outputs:

```yaml
strategy:
  fail-fast: false
  matrix:
    os: ${{ fromJSON(needs.process-analysis-config.outputs.os) }}
    config: ${{ fromJSON(needs.process-analysis-config.outputs.configurations) }}
```

The matrix is the Cartesian product of the configured operating systems and build configurations.

For example:

```text
OS                 Configuration
--------------------------------
windows-latest     Debug
windows-latest     Release
ubuntu-latest      Debug
ubuntu-latest      Release
```

`fail-fast: false` ensures that a failure in one matrix job does not cancel the remaining matrix jobs.

The job grants only the permissions required for the analysis and CodeQL operations.

---

### Analysis Sequence

Each matrix job performs the following sequence.

#### 1. Checkout Repository

The repository is checked out using:

```text
actions/checkout
```

Unlike the configuration-processing job, the analysis job requires the complete repository.

#### 2. Setup C++ Dependencies

The `setup-cpp-dependencies` action installs or prepares external C++ dependencies required by the project.

This action is intentionally separate from compiler and build-tool setup.

Examples include libraries such as Boost or spdlog.

#### 3. Setup C++ Environment

The `setup-cpp-environment` action prepares the compiler and analysis environment.

It provides the required LLVM/Clang tooling and Ninja build system, together with the appropriate compiler environment for the selected operating system.

On Windows, the Microsoft Visual C++ environment is initialized.

On Linux, Clang is used as the C++ compiler.

#### 4. Check Code Formatting

The `check-formatting` action verifies that the C++ source code conforms to the repository's clang-format configuration.

Formatting violations fail the analysis job.

#### 5. Configure CMake

The `configure-cmake` action configures the project using CMake and Ninja.

The selected matrix configuration is passed to the action:

```yaml
with:
  config: ${{ matrix.config }}
```

The configuration generates the compilation database required by clang-tidy.

#### 6. Initialize CodeQL

When CodeQL is enabled, the workflow initializes CodeQL before the build.

This step is skipped when:

```text
codeql: false
```

#### 7. Build Project

The `build-project` action builds the configured project using the selected configuration.

The build is performed independently of whether CodeQL is enabled.

#### 8. Verify Compilation Database

The workflow verifies that:

```text
build/compile_commands.json
```

was generated.

It also reports the number of compilation entries contained in the database.

The compilation database is required by the clang-tidy analysis.

#### 9. Run clang-tidy

The `run-clang-tidy` action performs the clang-tidy analysis using the generated compilation database.

The analysis:

* Runs in parallel.
* Uses all available processor cores.
* Treats warnings as errors.
* Restricts analysis to the project's source tree.

#### 10. Perform CodeQL Analysis

When CodeQL is enabled, the workflow performs the CodeQL analysis after the build.

Results are submitted using a matrix-specific category:

```text
/language:cpp/os:<os>/config:<config>
```

This keeps results from different matrix entries distinguishable.

#### 11. Upload Build Logs

When the matrix job fails, build log files are uploaded as an artifact.

The logs are retained for seven days.

#### 12. Upload Compilation Database

The generated compilation database is uploaded as a separate artifact and retained for seven days.

Keeping it separate from the build logs makes the compilation database independently available for investigating compilation and clang-tidy issues.

---

### CodeQL Behavior

CodeQL is controlled exclusively by the `codeql` property in:

```text
.github/static-code-analysis.json
```

When enabled, each matrix job:

1. Initializes CodeQL before the build.
2. Performs CodeQL analysis after the build.
3. Submits the CodeQL results using a matrix-specific category.

When disabled, the CodeQL steps are skipped.

---

### Artifacts

Each matrix job can produce two types of artifacts:

| Artifact (ZIP)                   | Content                 | Condition                      | Retention |
| -------------------------------- | ----------------------- | ------------------------------ | --------: |
| `build-logs-<os>-<config>`       | Build log files         | Job failure                    |    7 days |
| `compile-commands-<os>-<config>` | `compile_commands.json` | Compilation database generated |    7 days |

The `<os>-<config>` suffix identifies the matrix entry that produced the artifact.

The compilation database is preserved independently of the build-log artifact because it is useful for reproducing and investigating clang-tidy and compilation-related issues.

---

### Failure Behavior

The pipeline is intentionally strict.

Failures in configuration processing, formatting, compilation, or static analysis cause the corresponding matrix job to fail.

Matrix jobs are independent because `fail-fast` is disabled. A failure on one operating system or build configuration does not prevent the remaining matrix entries from running.

This provides complete cross-platform analysis results from a single workflow run.

---

## Composite Actions

Composite actions encapsulate the implementation details used by the workflow.

### `process-analysis-config`

```text
.github/actions/process-analysis-config/action.yml
```

Loads and validates:

```text
.github/static-code-analysis.json
```

Responsibilities:

* Parse the JSON configuration.
* Validate required properties.
* Validate supported operating systems.
* Validate supported build configurations.
* Validate the CodeQL flag.
* Reject empty matrix dimensions.
* Normalize values for GitHub Actions.
* Expose configuration as workflow outputs.

This action is intentionally responsible for validation rather than the workflow itself.

### `setup-cpp-dependencies`

```text
.github/actions/setup-cpp-dependencies/action.yml
```

Prepares optional external C++ dependencies required by the project.

The action is independent of compiler and build-tool installation.

It may perform no operation when the project does not require external dependencies.

### `setup-cpp-environment`

```text
.github/actions/setup-cpp-environment/action.yml
```

Prepares the C++ development and analysis environment.

The action provides:

* LLVM/Clang tooling.
* clang-format.
* clang-tidy.
* Ninja.
* The appropriate compiler environment.

The Windows environment uses MSVC together with LLVM analysis tools.

The Linux environment uses Clang.

### `check-formatting`

```text
.github/actions/check-formatting/action.yml
```

Checks the project's C++ source files against the repository's clang-format configuration.

Formatting differences cause the action to fail.

### `configure-cmake`

```text
.github/actions/configure-cmake/action.yml
```

Configures the CMake project for the selected build configuration.

The action:

* Uses Ninja.
* Selects the appropriate compiler for the operating system.
* Generates the build directory.
* Generates `compile_commands.json`.

### `build-project`

```text
.github/actions/build-project/action.yml
```

Builds the configured CMake project using Ninja.

The selected configuration is supplied by the matrix.

### `run-clang-tidy`

```text
.github/actions/run-clang-tidy/action.yml
```

Runs clang-tidy against the project using the generated compilation database.

The action delegates the parallel analysis to:

```text
.github/scripts/run-clang-tidy.py
```

The script is responsible for parallel execution across the available processor cores.

Additional implementation details are documented in:

```text
.github/scripts/RUN_CLANG_TIDY_README.md
```

---

## Usage

### Run the Workflow Manually

The workflow can be started from the GitHub Actions interface using **Run workflow**.

### Change the Analysis Matrix

Modify:

```text
.github/static-code-analysis.json
```

For example, to analyze only Windows Debug:

```json
{
  "os": [
    "windows-latest"
  ],
  "configurations": [
    "Debug"
  ],
  "codeql": true
}
```

No workflow modification is required.

### Disable CodeQL

Set:

```json
"codeql": false
```

The normal build, formatting, compilation database generation, and clang-tidy analysis continue to run.

### Run Analysis Locally

The individual components can also be run locally using the project's normal CMake workflow.

The GitHub Actions pipeline should be treated as the authoritative cross-platform validation environment because it exercises the configured runner environments and matrix.

---

## Extending the Pipeline

New functionality should normally be implemented as a composite action rather than by adding platform-specific implementation directly to the workflow.

For example:

```text
.github/actions/new-analysis-step/
└── action.yml
```

The workflow should then orchestrate the new action at the appropriate point in the analysis sequence.

Configuration-dependent behavior should be added to:

```text
.github/static-code-analysis.json
```

and processed by:

```text
process-analysis-config
```

This keeps configuration, orchestration, and implementation responsibilities separate.

---

## Design Principles

### Separation of Concerns

Each layer has a distinct responsibility:

```text
Configuration
    |
    +-- What should be analyzed?

Workflow
    |
    +-- When and in what order?

Composite Actions
    |
    +-- How is each operation performed?
```

### Configuration Is Independent

The analysis matrix and optional features should be configurable without modifying the workflow.

### Validation Is Explicit

Invalid configuration should fail clearly rather than silently falling back to defaults.

### Platform Details Stay in Actions

Operating-system-specific implementation belongs in composite actions wherever practical.

The workflow should remain readable as a high-level recipe.

### Matrix Jobs Are Independent

A failure in one OS/configuration combination should not prevent the other combinations from producing results.

### Reusable Components

Common operations should be implemented once and reused rather than duplicated across workflows.

---

## Maintenance

When modifying the CI/CD infrastructure:

1. Keep workflow orchestration high-level.
2. Put reusable implementation in composite actions.
3. Put analysis parameters in `.github/static-code-analysis.json`.
4. Keep configuration validation centralized in `process-analysis-config`.
5. Update `WORKFLOWS.md` when the CI/CD architecture or behavior changes.
6. Keep platform-specific behavior inside the appropriate composite action.
7. Avoid introducing implicit configuration defaults.

When adding a new analysis capability, first determine whether it belongs in configuration, workflow orchestration, or a composite action before modifying the pipeline.

---

## Contributing

Changes to the CI/CD infrastructure should be tested against all affected matrix combinations before being merged.

When changing the workflow architecture or behavior, update:

```text
.github/WORKFLOWS.md
```

to keep the documentation consistent with the implementation.

---

**Last Updated:** 2026-09-15
**Maintainer:** AmitGDev
