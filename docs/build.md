# Building and validating PipeForge

## Toolchains

Use CMake 3.25+, Ninja, Git and Python 3.10+. Windows x64 additionally needs MSVC
Build Tools with the desktop C++ workload and Windows SDK; the Visual Studio IDE
is optional. clang-cl/clang-tidy must match the selected SDK/STL. Linux supports
portable library/tests with a C++20 compiler; production named-pipe IPC is Windows-only.
clang-format is fixed to 19.1.5. Doxygen 1.9.8+ is optional for local API generation.

GitHub Actions uses `windows-2022` and `ubuntu-24.04`. Compiler and tool versions
are printed in logs. Actions and upstream sources are pinned; no dependency binary
cache is shared across toolchains.

## Windows build and application

```powershell
. ./scripts/Enter-Toolchain.ps1
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

The helper discovers an installed x64 toolchain with `vswhere` and modifies only
the current shell environment. An existing developer terminal works too. Repeat
with `windows-release` for optimized binaries or `windows-clang` for clang-cl.
Each preset owns a separate `build/<preset>` directory. Maintained targets use
C++20, strict warnings and warnings as errors.

Both executables require one explicit command. `--help` and `--version` report
capabilities; `--serve` starts the Windows host; client `--demo-sync`, `--demo-async`
and `--demo-mixed` run the complete process scenario. Invalid arguments return 2;
runtime/I/O failures return 1; success returns 0. See [usage and options](client.md).

## Portable Linux build

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

This covers contracts, codecs, domain/services, controlled client scheduling,
diagnostics and tooling. The Linux executable scaffolds expose help/version;
Win32 runtime commands report unsupported IPC.

## Dependencies and offline sources

CMake builds Google's matching Protobuf/protoc and compatible Abseil, plus spdlog
with bundled fmt and nlohmann/json. Test-enabled builds also fetch GoogleTest.
[The dependency inventory](third-party.md) records revisions, archive hashes and
licenses. No unversioned system runtime package is silently selected.

For fully offline acquisition, prepopulate each source at its pinned revision:

```powershell
cmake --preset windows-debug `
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=C:/deps/googletest `
  -DFETCHCONTENT_SOURCE_DIR_SPDLOG=C:/deps/spdlog `
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=C:/deps/json `
  -DFETCHCONTENT_SOURCE_DIR_ABSL=C:/deps/abseil `
  -DFETCHCONTENT_SOURCE_DIR_PROTOBUF=C:/deps/protobuf
```

These explicit overrides bypass acquisition/hash checks, so the supplied source
identity is the caller's responsibility. Reusing an already populated tree can use
`FETCHCONTENT_UPDATES_DISCONNECTED=ON`. A clean tree still needs matching source
inputs. First builds compile a substantial dependency graph; CI limits parallelism
to three to reduce peak memory.

`BUILD_TESTING=OFF` omits GoogleTest and Python-driven checks while retaining schema
generation. Application binaries use static runtime libraries and MSVC CRT; tests,
protoc, headers and development libraries are not included in release ZIPs.
`PIPEFORGE_ENABLE_PROFILING=OFF` compiles out scope macros while retaining ordinary
logs. See [diagnostic behavior](diagnostics.md).

## Formatting and static analysis

```powershell
python scripts/check_format.py
python scripts/check_format.py --fix
python -m unittest discover -s tests/tools -v
. ./scripts/Enter-Toolchain.ps1
cmake --preset windows-clang -DPIPEFORGE_BUILD_BENCHMARKS=ON
cmake --build --preset windows-clang
python scripts/check_tidy.py --build build/windows-clang
python scripts/check_analysis_probes.py
```

The format script checks maintained C++/schema sources with clang-format 19.1.5
and supplemental control/message spacing. Upstream/generated files are excluded.
The tidy script uses the real compilation database and fails on every enabled
project diagnostic; complete output is retained in the build tree. Matching tools
can be specified with `--clang-format` or `--clang-tidy`. Root builds also expose
`format-check` and, when clang-tidy is available, `static-analysis` targets.

See [coding standards](coding-standards.md) for analyzer coverage and narrow
exceptions. Python and CMake sources use logical sections, explanatory comments
and blank lines to separate operations; avoid compacting them into long blocks.

## Runtime instrumentation

Linux ASan/UBSan and leak detection use a separate tree:

```sh
cmake --preset linux-debug -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -DPIPEFORGE_SANITIZERS=ON
cmake --build --preset linux-debug
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --preset linux-debug -L 'unit|integration'
```

Race detection must use another tree, without ASan or fuzzing:

```sh
cmake --preset linux-tsan
cmake --build --preset linux-tsan
TSAN_OPTIONS=halt_on_error=1:exitcode=66 ctest --preset linux-tsan -L 'unit|integration'
```

The TSan CI job sets `vm.mmap_rnd_bits=28` on its disposable runner so older LLVM
runtimes can reserve shadow memory. This is a runner compatibility setting, not a
suppression of reported races. See [LLVM's TSan contract](https://clang.llvm.org/docs/ThreadSanitizer.html).
Instrumentation applies before dependencies are
created, including protoc and every linked consumer. The inheritance test inspects
actual compile commands and configure-only dependency graphs for every policy.
No TSan result for Windows is implied.

Windows ASan requires x64 clang-cl 22+, its matching runtime, and RelWithDebInfo:

```powershell
. ./scripts/Enter-Toolchain.ps1
# Prepend your LLVM 22+ bin directory if the selected workload has an older LLVM.
clang-cl --version
$resource = clang-cl -print-resource-dir
$env:PATH = "$resource/lib/windows;$env:PATH"
cmake --preset windows-asan
cmake --build --preset windows-asan
$env:ASAN_OPTIONS = 'halt_on_error=1:alloc_dealloc_mismatch=1'
ctest --preset windows-asan -L 'unit|integration|end-to-end'
```

Whole-build instrumentation preserves Protobuf's ASan-dependent message layout.
CMake explicitly links LLVM's matching static-CRT thunk/runtime because it invokes
the linker directly. CI installs official LLVM 22.1.3 with a verified SHA256;
that development runtime is absent from Release ZIPs. MSVC Debug/Release builds
remain supported; MSVC ASan is excluded because the pinned instrumented protoc
fails during static initialization. Older LLVM ASan runtimes fail captured C++
exception access, so Windows sanitizer builds require LLVM 22+.

Windows ASan does not replace Linux UBSan/leak detection or TSan. Handle-count
regressions complement native lifetime tests. Cross-logon process access requires
[the manual security check](windows-transport.md).

## Fuzzing and measurements

Normal CTest includes a deterministic 512-mutation decoder smoke. For libFuzzer:

```sh
cmake -S . -B build/linux-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang \
  -DPIPEFORGE_SANITIZERS=ON -DPIPEFORGE_BUILD_FUZZER=ON
cmake --build build/linux-fuzz --target pipeforge-protocol-fuzzer --parallel 3
python3 scripts/prepare_fuzz_corpus.py --output build/fuzz-corpus
./build/linux-fuzz/tests/pipeforge-protocol-fuzzer build/fuzz-corpus -max_total_time=30 -max_len=1048580
```

CI caps time, RSS and input length; scheduled runs last 600 seconds and retain
failure artifacts. `PIPEFORGE_BUILD_BENCHMARKS=ON` additionally builds
`pipeforge-protocol-benchmark`, which emits size/encode/decode CSV after round-trip
validation. It is a non-gating codec measurement, not a transport throughput claim.

[Test coverage and CI scope](testing.md), [Doxygen generation](api.md) and
[release packaging](releases.md) have dedicated guides.
