# M2 verification and measurement baseline

M2 implements the portable protocol boundary, not named-pipe feature parity. No commit, tag, push or release is created automatically. Linux jobs and libFuzzer execution require remote CI because this workstation has no Linux environment.

## Test-first evidence

The first protocol suite was compiled against stub payload codecs that returned failure. Eight of its 13 tests failed on missing round trips, version/presence handling, unknown-field handling and Protobuf merge semantics; framing and negative cases already passed. Implementing the upstream codec adapter made all 13 pass. Subsequent tests add adversarial groups/cardinality, exact payload limits, enum/metadata validation, golden bytes and deterministic fuzz invariants. The first toolchain compatibility test failed on a missing transitive utf8_range include; supplying that include made matching headers compile and Google's incompatible-runtime guard reject the deliberate mismatch.

The clang-tidy run caught integer-widening expressions, unnamed frame-header constants, test-only enum casts, and unchecked optional access in assertions. These were corrected rather than weakening project checks. Fixtures now use Protobuf's integer enum reflection API to construct unknown wire values. The deterministic mutation generator has a documented local fixed-seed exception; binary fixture literals have a local magic-number exception.

## Reproducible checks

The normal Debug/Release/Clang presets build the matching compiler/runtime, generate schemas in parallel and link both executable consumers. CTest covers all operation/outcome types, binary/semantic failures, incremental framing, the shared deterministic fuzz harness and the version-compatibility probe. Windows Release also installs and extracts all three ZIPs with exact manifests and required third-party notices, then starts the extracted programs using a system-only PATH.

The release build uses MSVC 19.44.35229 (Visual Studio 2022), CMake 3.31.6, Ninja and C++20 with `/W4 /WX`, static CRT and no LTO. The Clang build uses clang-cl/clang-tidy 22.1.3 with its matching Visual Studio SDK/STL. GoogleTest and all runtime sources are reused through explicit pinned FetchContent source overrides; generated files and compilation outputs start in a fresh Release directory.

Final local checks passed: 45 CTest entries in MSVC Release (25 unit, 19 integration, one isolated package end-to-end), 44 in clang-cl Debug, and all 18 protocol/framing/fuzz cases after the final adversarial regression. clang-tidy passed for all 14 selected translation units; the changed protocol test was checked again after adding the aggregate-node and embedded-NUL cases. clang-format 19.1.5, control spacing, actionlint and the 20-case Python tool suite passed. Linux sanitizer/libFuzzer execution remains pending remote CI; no Linux run is claimed.

## Initial non-gating baseline

Enable `PIPEFORGE_BUILD_BENCHMARKS=ON` in an optimized configuration and run `pipeforge-protocol-benchmark`. Each case checks a round trip before timing, then measures 2,000 complete adapter encodes and 2,000 decodes. The encoders include contract validation/conversion; these are not bare Protobuf microbenchmarks. The final checksum prevents unused encoded results. JSON is a compact, test-only semantic reference for byte counts, not a second IPC format or Google's ProtoJSON mapping. Both size columns exclude the common four-byte transport prefix.

One local Windows x64 MSVC Release run:

| Case | Protobuf bytes | JSON reference bytes | Encode ns/op | Decode ns/op |
| --- | ---: | ---: | ---: | ---: |
| Get object, ID 1 | 9 | 50 | 162 | 176 |
| Increment, delta -1 | 33 | 116 | 522 | 558 |
| 64 KiB ASCII text | 65,552 | 65,588 | 111,649 | 22,212 |
| 64 typed negative integer records | 519 | 1,557 | 10,093 | 10,284 |

These are single-run averages on a developer machine and carry no stable latency guarantee. Small controls and numeric batches are compact; an already long plain string barely shrinks. There is no compression. Allocation counts, cross-process latency, throughput and clock alignment are not measured here; retain these as later transport/quality measurements. No comparison of JSON CPU time or performance gate is claimed.

## Integration limits and reviewed dependency exceptions

The schemas/codecs use the full Protobuf runtime. Optional upstream examples, tests, unused targets and fallback downloads are excluded; runtime, Abseil and UTF-8 notices are included in each installed component. A Release dependency inspection found only Windows system DLLs (`KERNEL32.dll`, `dbghelp.dll`), with no application/Protobuf/CRT DLL to package. No Windows SDK DLL is copied into ZIPs.

Project warnings and analyzer families remain enabled. The pinned upstream build has narrow exceptions: remove unsupported `/MP` from clang-cl targets (Ninja owns parallelism); disable `invalid offsetof` only in Protobuf/generated targets that use its supported message layout; suppress MSVC C4715 only in the upstream parser source with an exhaustive enum switch and the JSON unparser whose fatal logger is nonreturning, C4624 only in the intentional dynamic-message union source, and C5287 only in the protoc library's upb C enum flag adapters. CMake 4's upstream CMP0141 deprecation notice is disabled within dependency configuration only. Downloaded/generated sources are not modified. Recheck these exceptions on a dependency update.

The Linux Clang ASan/UBSan configuration instruments all compiled targets and executable links, including Protobuf, libprotoc/protoc, Abseil, UTF-8, logging and test dependencies. This preserves the ASan-dependent Protobuf message layout across consumers; see the PR #4 corrections below. The separate libFuzzer job adds coverage instrumentation, a 30-second PR/master run and a 600-second weekly run, with crash/log artifacts. Local deterministic mutations do not substitute for those Linux jobs. Fuzzing/sanitizers are defect detectors, not proof of memory safety or a strict decoded heap quota.

Domain quotas, int64 mutation overflow checks, authorization/session isolation, dispatch, actual I/O, deadlines/cancellation, response correlation and R1-R8 process acceptance remain M3-M5 work. Generated API documentation and full delivery checks remain M6.

## PR #4 CI corrections

The first CI run of commit `ddf73afe15cefa6897d610185fb4338ac8159b13` failed before executing either Linux safety job. The sanitizer job failed compiling Abseil hash-policy template instantiations inside Protobuf with GCC/UBSan (`get_hash_slot_fn`: not a constant expression), matching [Abseil issue #1634](https://github.com/abseil/abseil-cpp/issues/1634). The fuzz job failed linking protoc: uninstrumented `libprotoc` referenced `FileDescriptorProto::_table_` and `GeneratedCodeInfo::_table_`, while ASan-instrumented `libprotobuf` used its alternate message globals. In the pinned Protobuf `port_def.inc`, `ABSL_HAVE_ADDRESS_SANITIZER` enables `PROTOBUF_MESSAGE_GLOBALS` and removes those separate parse-table definitions.

The correction applies ASan/UBSan compile and link flags at directory scope before any targets or dependency subdirectories exist. Per-target duplicates are removed. The regular sanitizer workflow explicitly selects Clang; unsupported GCC/UBSan configuration fails early with the upstream explanation, while GCC compiler-compatibility coverage remains. No dependency source edits, version changes, sanitizer suppressions or disabled checks are needed.

A configuration regression inspects every entry in the actual project compile database for consistent instrumentation. It also generates a small CMake dependency tree and inspects compile commands plus evaluated executable link options with sanitizers both ON and OFF. It covers a dependency runtime, its protoc-like executable and an application consumer, without requiring Linux sanitizer binaries to run on Windows. It runs as `sanitizer-configuration` under the integration label and explicitly in the fuzz workflow.

Local validation of this correction passed the clang-cl build and all 45 CTest entries (25 unit, 20 integration). After adding the production compile-database check, the regression was rerun successfully. clang-format 19.1.5, control spacing and actionlint also passed. Corrected Linux ASan/UBSan and libFuzzer execution remains pending a new CI run; Windows checks do not establish that those Linux jobs pass.
