# M1 verification

M1 delivers build/test/release infrastructure, not IPC feature parity. No project commit, tag, push, or GitHub release was created by this work.

## Test-first evidence

The CLI behavioral tests were written against a minimal implementation returning status 2. All five GoogleTest cases then failed on the intended missing output/behavior. Implementing help, build version, and explicit unsupported-invocation diagnostics made them pass.

The release-policy tests were written against stub normalization/provenance functions. The isolated repository tests and version cases failed on the expected missing validation. Implementing strict tag parsing, exact commit matching, and master ancestry checks made them pass. Additional archive tests cover exact manifests and rejection of source, symbols, test libraries, paths, missing executables, and Debug builds.

## Baseline local checks before the quality extension

- MSVC x64 Debug and Release: configure, build with warnings as errors, and all 10 CTest entries passed.
- clang-cl x64 Debug: configure, build with warnings as errors, and all 10 CTest entries passed.
- CTest entries comprise five C++ behavioral tests, four executable smoke tests, and a Python suite containing 13 release-tool tests.
- Formatting: clang-format 19.1.5 checked all maintained headers/sources successfully.
- Workflows: actionlint v1.7.7 validated both workflow definitions successfully.
- Release packaging: all three ZIP manifests inspected; each extracted independently and its executable(s) passed help/version startup with a system-only PATH.
- GoogleTest source override: additional configurations reused the pinned populated checkout without downloading another copy.
- Clean staged-source snapshot: configured and built independently with MSVC 2022 (19.44) and CMake 3.31.6, ran all tests, injected version `1.0.1`, and smoke-tested the three `V1.0.1` packages. This copy contained no original working-directory build output. The dependency source was supplied explicitly from the pinned checkout.
- Runtime dependency inspection: the clean Release client executable imports only `KERNEL32.dll`; no application or Visual C++ runtime DLL was required by the inspected binary.

Initial local tools were CMake 4.3 bundled with Visual Studio 2026, MSVC 19.51, clang-cl 22.1.3, and Python 3.12.14. The repository minimum remains CMake 3.25 and Python 3.10. GitHub runners use their recorded installed compiler/tool versions rather than these local absolute paths.

## Quality extension on the same branch

The [coding standards](coding-standards.md) now define explicit C++20 formatting, a Core Guidelines policy, enabled analyzer families, and narrow exceptions. Formatting was applied to all maintained C++ files and the version-header template. clang-tidy detected raw pointer arithmetic in the entry points and a portability warning for `#pragma once` in the maintained header. Bounded views and conventional include guards removed those findings. No broad exception handling was added to satisfy analysis.

Windows Release passed the suites independently: six unit CTest entries, five integration entries, and one isolated runtime-package end-to-end entry. The Python unit suite now also checks compilation-database selection and cross-platform project-header filtering. Real Git provenance tests were separated into the integration suite.

clang-tidy analyzed all four maintained translation units with enabled findings treated as errors. This passed with LLVM 22.1.3 and also through the CMake `static-analysis` target using LLVM 19.1.5 with its matching MSVC 2022 SDK/STL environment; all eleven Debug CTest entries passed in the latter configuration. Analyzer activation probes confirmed diagnostics for leak, use after free, division by zero, and uninitialized return on both tool versions. Temporary defect snippets were analyzed only, never executed. The workflow definitions, including reusable workflows and seven independent status workflows, passed actionlint.

README badges use the verified current remote URL and explicitly select `branch=master&event=push`; they cannot report a feature-branch or PR run as master status. Status becomes available only after the corresponding workflow runs on master.

## Logging, profiling, and spacing completion

The first three diagnostic tests failed against the stub backend (missing text output, missing I/O error, and missing scope events). The implemented spdlog adapter made them pass. Expanded tests then parsed the complete JSON document and caught an extra opening delimiter; correcting the fixed-format header made those tests pass. Real CLI tests verify setup errors, aliases, required options, and explicit file output.

The final library uses pinned, SHA256-verified spdlog/fmt and nlohmann/json sources. Both executables link the same static diagnostic target. Tests cover nested/unwound scopes, event limits, Unicode/escaping, concurrent thread tracks, deferred shutdown, and disabled macro argument evaluation. File/diagnostic adapter cases carry the integration label; CLI string/option logic and disabled macro behavior remain unit checks.

The profiling-on Windows Release configuration passes all 26 CTest entries, including isolated package log/trace generation and parsing. A global profiling-off clang-cl configuration also builds and passes its applicable tests; trace-producing tests are intentionally skipped while macro side-effect, text log, and CLI rejection tests execute. CI now includes this variant. clang-tidy checks eight maintained translation units, including the diagnostic implementation and tests.

The integrated formatting command applies/checks one empty line after completed control-flow blocks and between function definitions. Seven spacing regression cases cover chains, loops, comments/literals/raw strings, macros, enclosing braces, and idempotence. The same command and policy run in CI. The three release ZIPs retain exact executable manifests plus required third-party notices; diagnostics are tested from extracted runtimes with a system-only PATH.

Example outputs are reproducible through [diagnostic usage](diagnostics.md); local examples live under ignored `build/examples/`. No release or tag was published by this work.

## PR #3 CI corrections

The first [PR #3](https://github.com/ocarizr/StreamLabs-Test-Assingment/pull/3) runs at commit `153a2aed78d6db21e8e619a1e9cf798c69d82a5a` exposed two compilation issues before tests could run:

- Windows MSVC 19.44 rejected constant profiling conditions with C4127 under `/WX`. This blocked component builds and the unit/integration/end-to-end jobs; the disabled build also reported it in the diagnostic backend. Compile-time `if constexpr` now selects profiling support before evaluating runtime paths.
- GCC rejected the temporary ranges expression in both entry points with `-Wdangling-reference`. Naming the argument view gives it an explicit lifetime through iteration. The strict warning policy remains enabled.

The original clang-tidy job passed; its large warning totals came from diagnostics excluded by the project-header/system-header filters. Complete raw logs are now preserved as an artifact, actionable output remains visible, and analyzer exit failures still block CI. Build, analysis, and defect probes have separate workflow steps. Checkout and the diagnostic-log upload now use pinned Node 24 Actions, addressing the observed checkout Node 20 deprecation warning.

Local reproduction uses MSVC 19.44/Visual Studio 2022, matching the failing Windows runner generation: clean Debug passes all 25 entries, Release all 26 including isolated ZIP execution, and profiling-off passes 18 applicable entries with seven trace-only cases skipped. Independent client/server targets also build and start in Debug and Release with `BUILD_TESTING=OFF`. clang-cl 19 and 22 each build and pass 25 entries; matching clang-tidy 19.1.5 and 22.1.3 each check all eight translation units and pass all four defect probes. The LLVM 19 run reproduces the excluded-header totals in the retained raw log while emitting no project findings. The Python tool suite passes 20 cases, including raw-output retention, diagnostic visibility, and nonzero analyzer exit propagation. Formatting and actionlint checks pass.

## Remaining verification limits

The initial workflows ran on GitHub; the corrections above await a new commit/push and fresh PR checks. The Linux compilation failure has been addressed in source, but Linux execution and sanitizer results still require the remote rerun; no local Linux environment is installed. Publication itself awaits a user push of a supported tag on master history. Local ZIP startup testing checks the available Windows installation, not every possible target host.

The Linux ASan/UBSan/leak-detection configuration is implemented but awaits Linux CI execution. Static analysis is not proof that all undefined behavior or leaks are absent. Named-pipe connection, protocol serialization, Windows transport sanitizer coverage, fuzzing, and generated API documentation remain later work. The then-retained legacy sources were not executed or included in the new build or packages; they were subsequently removed during the pre-M3 cleanup.
