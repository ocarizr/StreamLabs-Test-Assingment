# M1 verification

M1 delivers build/test/release infrastructure, not IPC feature parity. No project commit, tag, push, or GitHub release was created by this work.

## Test-first evidence

The CLI behavioral tests were written against a minimal implementation returning status 2. All five GoogleTest cases then failed on the intended missing output/behavior. Implementing help, build version, and explicit unsupported-invocation diagnostics made them pass.

The release-policy tests were written against stub normalization/provenance functions. The isolated repository tests and version cases failed on the expected missing validation. Implementing strict tag parsing, exact commit matching, and master ancestry checks made them pass. Additional archive tests cover exact manifests and rejection of source, symbols, test libraries, paths, missing executables, and Debug builds.

## Local checks

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

## Limits of this verification

The workflows have been linted locally but have not run on GitHub. The Linux job awaits remote execution; no local Linux environment is installed. Publication itself awaits a user push of a supported tag on master history. Local ZIP startup testing checks the available Windows installation, not every possible target host.

Named-pipe connection, protocol serialization, memory sanitizers, clang-tidy, fuzzing, and generated API documentation belong to later milestones. The retained legacy sources were not executed and are not included in the new build or packages.
