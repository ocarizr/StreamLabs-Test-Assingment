# Building PipeForge

## Prerequisites and supported targets

- CMake 3.25 or newer, Ninja, Git, and Python 3.10 or newer.
- Windows x64: MSVC Build Tools with the desktop C++ workload and Windows SDK. clang-cl is optional for the alternate compiler preset. No Visual Studio IDE is required.
- Linux: a C++20 compiler, for portable scaffold/core validation only. The intended IPC runtime remains Windows.
- clang-format 19.1.5 for reproducible formatting. For example, install the standalone tool with `python -m pip install clang-format==19.1.5` and ensure its executable directory is on PATH.
- clang-tidy 19 or newer from a compiler toolchain compatible with the selected Windows SDK/STL for static analysis.

GitHub Actions uses `windows-2022` and `ubuntu-24.04`. Runner toolchain updates are managed by GitHub; configure logs record compiler versions. Actions and GoogleTest sources are fixed to immutable revisions. Local validation details are recorded in [M1 verification](m1-verification.md).

## Windows

Run from the repository root in PowerShell:

```powershell
. ./scripts/Enter-Toolchain.ps1
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

The helper discovers an installed C++ toolchain using `vswhere`, initializes an x64 developer shell, and adds bundled CMake/Ninja/LLVM directories when present. You can instead use an existing x64 developer terminal with these tools on PATH. It changes only the current shell environment.

Repeat with `windows-release` for optimized builds or `windows-clang` for clang-cl. Both C and C++ compiler selections are explicit because GoogleTest also enables C during its configuration. Each preset has its own `build/<preset>` directory. All maintained C++ targets require C++20 and enable strong warnings as errors by default. Use `-DPIPEFORGE_WARNINGS_AS_ERRORS=OFF` only for local investigation.

The initial executables accept exactly one of `--help` or `--version`. Invoking them without arguments or with unsupported arguments returns status 2; no connection or listener is started. This is a feature boundary, not a successful IPC demonstration.

## Linux portable scaffold

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

Linux tests validate the same CLI and release-policy logic. Later Windows pipe tests will remain Windows-only.

## Dependencies and offline builds

`cmake/Dependencies.cmake` builds GoogleTest v1.17.0 at commit `52eb8108c5bdec04579160ae17225d66034bd723`. It has no enabled external transitive dependencies in this configuration. GoogleMock and upstream installation are disabled. GoogleTest is never linked into the application executables.

For an offline configure, prepopulate that exact checkout and use:

```powershell
cmake --preset windows-debug "-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=C:/deps/googletest"
```

The offline source override is explicit and must match the pinned revision; it bypasses FetchContent acquisition. A previously populated build can be configured with `-DFETCHCONTENT_UPDATES_DISCONNECTED=ON`. New CI builds acquire the pinned sources directly; no cross-toolchain binary cache is used in M1. A future dependency cache must include compiler, runtime, architecture, and dependency revisions in its key.

Application-only builds can use `-DBUILD_TESTING=OFF` and do not fetch GoogleTest or require Python to compile. Python remains necessary for formatting and release tooling. Protobuf and its transitive dependency graph are intentionally deferred to M2.

## Formatting and tests

```powershell
python scripts/check_format.py
cmake --build build/windows-debug --target format-check
python -m unittest discover -s tests/tools -v
```

The format script rejects a different tool version and checks maintained sources under `apps`, `include`, `src`, and `tests/unit`, plus `cmake/version.hpp.in`. Upstream/generated output and archived `Client`/`Server` snapshots are excluded. The CMake target is available when Python is discoverable. The [coding standards](coding-standards.md) describe the explicit C++ style and semantic policy.

CTest separates unit behavior from process/Git integration and isolated package end-to-end validation. Release provenance tests create temporary Git repositories; they never change this project's history. CTest fails if no matching tests are discovered and applies time limits.

```powershell
ctest --preset windows-debug -L unit
ctest --preset windows-debug -L integration
ctest --preset windows-release -L end-to-end
```

End-to-end validation requires a built Windows Release configuration. It installs client/server components into temporary directories and verifies the three distributions independently of other tests.

## Static analysis and runtime instrumentation

```powershell
. ./scripts/Enter-Toolchain.ps1
cmake --preset windows-clang
cmake --build --preset windows-clang
python scripts/check_tidy.py --build build/windows-clang
python scripts/check_analysis_probes.py
cmake --build build/windows-clang --target static-analysis
```

Use a matching clang-cl/clang-tidy/SDK environment. The analysis script supports `--clang-tidy <path>`. The CMake target is available when Python and clang-tidy are discoverable; set the `PIPEFORGE_CLANG_TIDY` CMake cache entry to override its executable. All maintained translation units, including test code, are analyzed; all enabled findings fail the check.

On Linux, the runtime safety configuration is:

```sh
cmake --preset linux-debug -DPIPEFORGE_SANITIZERS=ON
cmake --build --preset linux-debug
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --preset linux-debug -L 'unit|integration'
```

The combined sanitizer option is intentionally rejected on Windows and unsupported compilers. Linux instrumentation does not cover Win32 named-pipe code. See the coding standards for check coverage and exceptions.

Windows builds use the static MSVC CRT consistently across application and test libraries. M1 runtime ZIPs therefore need only their executable files and standard Windows system libraries. A later runtime dependency must add install rules, a reviewed package manifest, applicable notices, and isolated extraction tests.

## Current CI scope

Pull requests and pushes to `master` run separate client/server build, formatting, unit, integration, end-to-end, and static-analysis/sanitizer workflows, plus a compiler compatibility matrix. README badges report only master push runs, not PR results. Reusable workflows share component-build and test-suite plumbing. Fuzzing, real IPC integration, Windows transport sanitizers, and Doxygen coverage remain later work. Release publishing is separate and documented in [releases](releases.md).
