# Building PipeForge

## Prerequisites and supported targets

- CMake 3.25 or newer, Ninja, Git, and Python 3.10 or newer.
- Windows x64: MSVC Build Tools with the desktop C++ workload and Windows SDK. clang-cl is optional for the alternate compiler preset. No Visual Studio IDE is required.
- Linux: a C++20 compiler, for portable scaffold/core validation only. The intended IPC runtime remains Windows.
- clang-format 19.1.5 for reproducible formatting. For example, install the standalone tool with `python -m pip install clang-format==19.1.5` and ensure its executable directory is on PATH.

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

The format script rejects a different tool version and checks only maintained sources under `apps`, `include`, `src`, and `tests/unit`. Upstream/generated sources and archived `Client`/`Server` snapshots are excluded. The CMake target is available when Python is discoverable.

CTest runs five GoogleTest behavioral cases, four executable smoke tests, and the Python release-tool suite. Release tests create isolated temporary Git repositories to verify real ancestry/tag behavior; they never commit to or change this project's history. CTest fails if no tests are discovered and applies time limits.

Windows builds use the static MSVC CRT consistently across application and test libraries. M1 runtime ZIPs therefore need only their executable files and standard Windows system libraries. A later runtime dependency must add install rules, a reviewed package manifest, applicable notices, and isolated extraction tests.

## Current CI scope

Pull requests and pushes to `master` run MSVC Debug/Release, clang-cl, the portable Linux build, formatting, and release package smoke tests. The initial job does not claim clang-tidy, sanitizer, fuzzing, IPC integration, or Doxygen coverage; those remain in the planned milestones. Release publishing is separate and documented in [releases](releases.md).
