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

## Remaining verification limits

The workflows have been linted locally but have not run on GitHub. The Linux job awaits remote execution; no local Linux environment is installed. Publication itself awaits a user push of a supported tag on master history. Local ZIP startup testing checks the available Windows installation, not every possible target host.

The Linux ASan/UBSan/leak-detection configuration is implemented but awaits Linux CI execution. Static analysis is not proof that all undefined behavior or leaks are absent. Named-pipe connection, protocol serialization, Windows transport sanitizer coverage, fuzzing, and generated API documentation remain later work. The retained legacy sources were not executed and are not included in the new build or packages.
