# M3 verification

M3 implements in-process domain/services behavior for R3/R4/R6/R7/R8. It introduces no named-pipe I/O, async connection/call API, session scheduler or cross-process acceptance claim. See [domain/services](domain-services.md) for ownership, quotas and extension contracts.

## Test-first evidence

The initial 11 domain/service tests compiled and ran against stubs. All 11 failed on missing typed storage, class metadata, stateful invocation, isolation/lifecycle, quotas, ID exhaustion and correlated dispatch. The first implementation made all 11 pass. Additional adversarial and lifecycle tests bring M3 to 20 unit cases and three codec/service integration cases.

Tests exercise all ten operations through actual Protobuf encode/decode and the dispatcher, plus metadata discovery, exact named argument matching independent of order, wrong/missing/duplicate arguments, invalid UTF-8/nonfinite/oversized direct values, positive/negative overflow without mutation, independent object instances, foreign/stale/released IDs, disconnect cleanup and server-lifetime ID exhaustion.

Custom probe factories verify registration extensibility without dispatcher edits, null/failed factories, null clones, wrong result/state contracts, errors after candidate mutation, old-state retention and destruction of every owned instance/candidate. Shared-byte and count quotas include released-capacity reuse, rejected growth, maximum text append and hard ceilings despite oversized configuration. Aggregate metadata is rejected before exceeding the codec node budget. Large pages deliver every stored value while every response remains encodable below the payload limit.

Diagnostic integration checks fixed operation labels, correlation IDs and error categories, excludes peer values and class names from logs/traces, and validates profiling on/off behavior. The initial profiling-off run exposed a test assumption that a trace file always existed; the corrected test explicitly verifies its absence while still checking ordinary logs.

## Executed checks

Local validation uses the existing pinned dependency source overrides, CMake/Ninja, clang-cl/clang-tidy 22.1.3 and Visual Studio 2022 MSVC Release with strict project warnings. No dependency pin, wire version, schema field, release trigger or publishing permission changed.

| Configuration/check | Result |
| --- | --- |
| clang-cl Debug | 68 CTest entries passed: 45 unit, 23 integration |
| MSVC 2022 Release | 69 CTest entries passed: 45 unit, 23 integration, one isolated package end-to-end |
| MSVC 2022 Release, profiling disabled | 68 entries selected; 61 passed and seven existing trace-only cases skipped; all M3 cases passed |
| clang-tidy 22.1.3 | All 22 maintained translation units passed with warnings treated as errors |
| clang-format 19.1.5 and control/schema spacing | Passed |
| GitHub Actions syntax (actionlint) | Passed |

The static-analysis pass identified special-member/observer contracts, a throwing MSVC tree move in the startup registry, optional assertions in tests, a needless copy and a do/while loop. Corrections keep all enabled checks: explicit ownership contracts, a bounded sorted-vector registry, guarded optional access and ordinary iteration. No new suppression was added outside the established magic-number scope for behavioral test fixtures.

Existing unit/integration workflows and the portable sanitizer job discover the new CTest labels automatically. The server alone links the services target; the client continues linking contracts/protocol/diagnostics without domain/service code. Decoder fuzz instrumentation now includes shared semantic validation after its extraction from the codec.

Linux ASan/UBSan/leak detection and libFuzzer execution for this M3 branch remain pending remote CI: this workstation has no Linux runtime. These Windows results do not establish Linux sanitizer success or Windows named-pipe safety. Logical state accounting is not an exact heap quota, and trusted custom C++ implementations are not sandboxed.
