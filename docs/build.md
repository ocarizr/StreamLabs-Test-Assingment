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

`cmake/Dependencies.cmake` acquires spdlog v1.17.0 (with bundled fmt) and nlohmann/json v3.12.0 from immutable archives verified by SHA256. They are used by the common static diagnostic library. Test-enabled builds also acquire GoogleTest v1.17.0 at commit `52eb8108c5bdec04579160ae17225d66034bd723`; GoogleMock and upstream installation are disabled. GoogleTest is never linked into the application executables. See [dependency notices](third-party.md) for revisions and licenses.

For an offline configure, prepopulate that exact checkout and use:

```powershell
cmake --preset windows-debug "-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=C:/deps/googletest"
```

Runtime sources must also be prepopulated for a fully offline configure: pass `-DFETCHCONTENT_SOURCE_DIR_SPDLOG=C:/deps/spdlog`, `-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=C:/deps/json`, `-DFETCHCONTENT_SOURCE_DIR_ABSL=C:/deps/abseil` and `-DFETCHCONTENT_SOURCE_DIR_PROTOBUF=C:/deps/protobuf`, each at its pinned revision. No unversioned system runtime dependency is silently selected. M2 builds official protoc 36.2 and its static full runtime, compatible Abseil and bundled utf8_range; see exact revisions/archive hashes in the notices document.

The offline source override is explicit and must match the pinned revision; it bypasses FetchContent acquisition. A previously populated build can be configured with `-DFETCHCONTENT_UPDATES_DISCONNECTED=ON`. New CI builds acquire the pinned sources directly; no cross-toolchain binary cache is used in M1. A future dependency cache must include compiler, runtime, architecture, and dependency revisions in its key.

Application-only builds can use `-DBUILD_TESTING=OFF` and do not fetch GoogleTest or require Python to compile. They still build diagnostics, Protobuf and protoc for schema generation. Python remains necessary for quality/release tooling. Generated files stay under `generated/wire` in the build tree. JSON is used for traces and a benchmark-only size reference, never IPC.

## Formatting and tests

```powershell
python scripts/check_format.py
python scripts/check_format.py --fix
cmake --build build/windows-debug --target format-check
python -m unittest discover -s tests/tools -v
```

The format script rejects a different tool version and checks maintained sources under `apps`, `include`, `src`, `tests` and `benchmarks`, schemas under `proto`, and `cmake/version.hpp.in`. C++ uses the house style and schemas use a two-space Protobuf style with one empty line between message/enum definitions. It combines clang-format with a lexical blank-line rule after complete braced control blocks; `--fix` applies both policies. Functions are separated by clang-format's definition-block policy. The Protobuf definition-block policy is configured in `.clang-format`; a lexical supplement covers message gaps that clang-format 19 does not insert. `--fix` applies the gaps and CI checks them, including before attached documentation. Upstream/generated output is excluded. The CMake target is available when Python is discoverable. See [coding standards](coding-standards.md).

CTest separates unit behavior from codec/service and process/Git integration, plus isolated package end-to-end validation. M3 domain tests use the unit label; full codec/dispatcher round trips, bounded page encoding and safe diagnostic output use integration. Domain links only shared contracts/UTF-8 validation; the server alone links the services target. See [domain/services](domain-services.md). Release provenance tests create temporary Git repositories; they never change this project's history. CTest fails if no matching tests are discovered and applies time limits.

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
cmake --preset linux-debug -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -DPIPEFORGE_SANITIZERS=ON
cmake --build --preset linux-debug
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --preset linux-debug -L 'unit|integration'
```

The combined sanitizer option requires Linux Clang. GCC remains covered by the normal compiler-compatibility build; GCC/UBSan cannot compile the pinned Abseil constexpr hash policy ([upstream issue #1634](https://github.com/abseil/abseil-cpp/issues/1634)). Sanitizer compile/link options apply before any targets are created, including Protobuf, libprotoc/protoc, Abseil, UTF-8, logging and test dependencies. Protobuf 36.2 changes its message layout under ASan, so mixing instrumented and uninstrumented consumers is incompatible. No sanitizer checks are disabled. Linux instrumentation does not cover Win32 named-pipe code. See the coding standards for check coverage and exceptions.

Windows builds use the static MSVC CRT consistently across application and test libraries. Runtime ZIPs contain executables plus `THIRD-PARTY-NOTICES.txt`; spdlog/fmt/JSON are statically linked and add no runtime DLLs. A later dynamic dependency must extend the installation/manifest and isolated extraction tests. [Diagnostic options](diagnostics.md) document logs, profiling macros, lifecycle, and `PIPEFORGE_ENABLE_PROFILING=OFF`.

## Protocol fuzzing and measurements

Normal CTest builds include a deterministic 512-mutation smoke using the shared invariant harness. For coverage-guided fuzzing in a separate Linux Clang configuration:

```sh
cmake -S . -B build/linux-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang \
  -DPIPEFORGE_SANITIZERS=ON -DPIPEFORGE_BUILD_FUZZER=ON
cmake --build build/linux-fuzz --target pipeforge-protocol-fuzzer --parallel 3
python3 scripts/prepare_fuzz_corpus.py --output build/fuzz-corpus
./build/linux-fuzz/tests/pipeforge-protocol-fuzzer build/fuzz-corpus -max_total_time=30 -max_len=1048580
```

The fuzz option requires Linux Clang, testing enabled and sanitizers. CI uses bounded concurrency, time/RSS/input limits and artifacts; the weekly run lasts 600 seconds. The regular sanitizer job runs unit/integration tests independently.

```powershell
cmake --preset windows-release -DPIPEFORGE_BUILD_BENCHMARKS=ON
cmake --build --preset windows-release --target pipeforge-protocol-benchmark
./build/windows-release/pipeforge-protocol-benchmark.exe
```

The optional benchmark emits size/encode/decode CSV, validates round trips and has no performance thresholds. The static-analysis workflow also compiles/analyzes it. See the [protocol guide](protocol.md) and [M2 verification record](m2-verification.md). First builds compile a substantial upstream compiler/runtime graph; jobs allow longer timeouts and cap build parallelism at three to limit memory use.

## Current CI scope

Pull requests and pushes to `master` run separate client/server build, formatting, unit, integration, end-to-end, static-analysis/sanitizer and protocol-fuzzing workflows, plus compiler compatibility and profiling-disabled configurations. README badges report only master push runs. Reusable workflows share plumbing. Real Windows pipe/security/lifecycle tests run in integration; the static-analysis workflow also runs whole-build clang-cl/ASan on Windows. Doxygen coverage remains later work. Publishing is separate and documented in [releases](releases.md).


## Windows transport safety

M4 builds the native libraries and GoogleTest pipe suite only on Windows. Endpoint
validation and completion-policy unit tests remain portable. See the
[transport contract](windows-transport.md) and [M4 results](m4-verification.md).

Use x64 clang-cl 22+ with its matching ASan runtime (the C++ workload must include
LLVM and sanitizer libraries):

```powershell
. ./scripts/Enter-Toolchain.ps1
# If necessary, prepend the bin directory of your LLVM 22+ installation here.
clang-cl --version
$resource = clang-cl -print-resource-dir
$env:PATH = "$resource/lib/windows;$env:PATH"
cmake --preset windows-asan
cmake --build --preset windows-asan
$env:ASAN_OPTIONS = 'halt_on_error=1:alloc_dealloc_mismatch=1'
ctest --preset windows-asan -L 'unit|integration'
```

The separate RelWithDebInfo tree instruments dependencies, protoc, generated code,
applications and tests before any target is created. It preserves frame pointers
and explicitly links the matching LLVM runtime because CMake invokes the linker
directly. This development runtime is not added to Release ZIPs. MSVC's normal
Debug/Release configurations remain supported; MSVC ASan is excluded after a
reproduced Protobuf 36.2 static-initialization failure. No sanitizer checks or
Protobuf instrumentation are disabled to work around it. See
[LLVM's Windows runtime linkage](https://clang.llvm.org/docs/UsersManual.html#finding-clang-runtime-libraries).

Windows ASan does not supply the Linux UBSan/leak-detection coverage. Native handle
cleanup is additionally checked over repeated pipe lifecycles; exact-zero handle
counts cannot be inferred from ASan. Cross-account/logon process checks require a
manual second account and are described in the transport guide.

The Windows ASan job installs official LLVM 22.1.3 using a fixed release URL and
SHA256 (`72ca54c3b7940615776d8994c0b918a1d8c4981bdcf13b439eaa86a1bd477001`).
The older LLVM 19 static sanitizer runtime failed captured C++ exception access in
local testing, so Windows ASan requires LLVM 22+ and RelWithDebInfo. This constraint
does not change normal Debug/Release or clang-tidy compiler support.

## Client/runtime validation

M5 builds `pipeforge-client-api` on all supported build platforms. Its scheduler,
correlation and cancellation tests inject controlled framed-payload peers; native
composition and IPC executables are Windows-only. See the [client guide](client.md).

```powershell
ctest --preset windows-clang -R 'Client\.|WindowsClient\.'
ctest --preset windows-release -L end-to-end
```

The end-to-end label includes real R1–R8 server/client processes. Release adds
runtime ZIP installation/extraction checks and repeats IPC for the split packages
and bundle with a system-only PATH. The Windows ASan job includes this process
label alongside unit/integration tests. Linux tests exercise the portable client
with controlled peers; actual Win32 IPC still requires Windows.
