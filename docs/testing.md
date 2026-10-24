# Tests and requirement traceability

Build first using [the build guide](build.md). CTest labels separate unit,
integration and end-to-end evidence:

```powershell
ctest --preset windows-debug -L unit
ctest --preset windows-debug -L integration
ctest --preset windows-release -L end-to-end
ctest --preset windows-clang -R 'Client\.|WindowsClient\.'
```

## Functional requirements

Every row includes `ipc-process-acceptance`, which launches actual Windows
client/server processes, runs sync/async/mixed modes against one server and starts
two mixed clients simultaneously. The named tests below isolate additional
properties that a successful demo alone cannot establish.

| Requirement | Specific CTest evidence | Observable behavior |
| --- | --- | --- |
| R1 | `ipc-process-acceptance`; `PipeTransport.RoundTripsTypedValuesAndKeepsSessionState`; `PipeNative.ClientConnectedBeforeAcceptIsAValidConnection` | Local named-pipe connection, framed exchange, immediate/pending connection completion and clean shutdown. |
| R2 | `ipc-process-acceptance`; `Client.ConnectAndCallReturnPendingFuturesWithoutWaitingForThePeer`; `Client.CorrelatesOutOfOrderRepliesAndUsesTheSamePathForBlockingCalls` | Futures remain pending while caller work proceeds; blocking calls share response/error semantics; reversed replies reach the correct callers. |
| R3 | `ipc-process-acceptance`; `Protocol.PreservesIntegerBoundariesEmptyStringsAndSignedZero`; `Protocol.RejectsInvalidNumbersEnumsAndIdentifiers` | Empty/UTF-8 text, signed integer limits and finite doubles retain their types; malformed/nonfinite values are rejected. |
| R4 | `ipc-process-acceptance`; `domain_test.CounterRetainsIndependentStateAndChecksArguments`; `domain_test.RejectsBothIntegerOverflowsWithoutMutation` | Construction, snapshots, attributes and method calls preserve independent state; wrong arguments and overflow do not mutate it. |
| R5 | `ipc-process-acceptance`; `Client.PendingConnectCancellationAndPreexpiredConnectDoNotPublishSuccess`; `PipeFailures.IndependentSessionsDispatchConcurrentlyAndPreserveOwnership` | Both connection APIs and mixed call styles share one live server; multiple sessions are bounded and isolated; cancelled/expired connection attempts cannot publish success. |
| R6 | `ipc-process-acceptance`; `domain_test.PreservesTypedValuesAndExclusivePagination`; `domain_test.CountAndSharedByteQuotasRejectBeforeMutation`; `ServiceIntegration.ByteBoundedPagesDeliverEveryValueWithoutOversizedResponses` | Typed values remain retrievable by ID and exclusive cursor; count/byte quotas and response-size bounds apply before committing mutations. |
| R7 | `ipc-process-acceptance`; `domain_test.SecondClassRequiresNoDispatcherChanges`; `Registry.DuplicateInvalidMetadataAndFactoryFailure`; `Registry.ConstructorParametersMatchByNameNotPosition` | TextBuffer registers through the same interface as Counter; factories/metadata are validated and clients discover their contracts. |
| R8 | `ipc-process-acceptance`; `domain_test.SessionIsolationReleaseAndDisconnectInvalidateIds`; `DomainFailures.ReleaseCloseAndDestructorDestroyAllOwnedInstances` | Session-owned instances retain state, cannot be accessed by another session, and are destroyed by release, close or store destruction. |

`runtime-packages` repeats process acceptance with extracted client/server ZIPs and
the combined bundle, a system-only PATH and fresh working directories. It also
checks exact manifests, versions, notices and opt-in diagnostics. This test is
registered only for Windows Release; process acceptance also runs in Debug/ASan.

The documentation checker validates local links and these CTest names against the
source tree. A removed/renamed case or a missing requirement fails the check.

## Failure, lifetime and resource coverage

Client unit tests inject controlled framed-payload peers for response ordering,
unknown/duplicate IDs, unexpected outcome variants, malformed bytes, concurrent
submitters, admission quotas, queued/sent cancellation, deadline and shutdown
completion. Native integration fills all sixteen pending slots with maximum-length
strings to exercise duplex backpressure, then checks idle-reader shutdown and 32
repeated client/server lifecycles for handle accumulation.

Transport tests cover fragmented/concatenated frames, zero/oversized lengths,
partial-frame timeout, abrupt peer process exit, occupied endpoint ownership,
restricted-token denial, protected logon-SID DACLs and cancellation followed by
mandatory terminal drain. The completion-policy backend controls cancellation
races. A warmed-up handle-count test allows a small runtime slack and detects
accumulation; it does not prove that every one-off leak is absent.

Protocol tests cover golden independent byte fixtures, every operation/outcome,
unknown-field compatibility, Protobuf merge semantics, UTF-8 and collection/parser
bounds. The compiler/runtime probe intentionally rejects mismatched generated
headers. Domain tests cover rollback, invalid trusted factories and ID exhaustion.
Diagnostic tests include real file failures and deferred/concurrent scope cleanup.

## CI matrix

| Validation | Runtime and scope |
| --- | --- |
| Independent client/server builds | Windows MSVC Debug/Release, testing disabled. |
| Compiler compatibility | Windows MSVC Debug/Release, clang-cl, portable Linux GCC; profiling on/off. |
| Unit/integration/end-to-end | Separate Windows workflows; Release adds extracted runtime packages. |
| Static analysis | Matching Windows clang-tidy across all maintained translation units; activation probes for representative defects. |
| Address/undefined/leak detection | Whole-build Linux Clang ASan/UBSan with leak detection; separate Windows clang-cl ASan including real pipe/process tests. |
| Race detection | Separate whole-build Linux Clang TSan, including portable client and concurrent diagnostics tests. |
| Decoder fuzzing | Linux Clang libFuzzer with ASan/UBSan; bounded PR/master smoke and longer weekly run. |
| Documentation | Local links, requirement references and public Doxygen generation; warnings fail and HTML is uploaded. |
| Dependencies | Immutable-source checks plus OSV advisory queries on PR/master/weekly/manual runs; report artifact retained. |
| Release | Tag provenance, exact-commit Windows Release build/test, three runtime ZIPs, then separate publication job. |

All workflows use read-only tokens except the isolated release publication job.
Actions are pinned to commit SHAs. Remote workflow results must be inspected after
pushing; local runs and workflow configuration are not remote CI results.

## Limits of the evidence

Linux checks exercise portable libraries and controlled client peers, not Win32
IPC. Windows ASan does not provide Linux UBSan/TSan/leak detection. TSan and ASan
use separate build trees. Static analysis, sanitizers and fuzzing detect classes
of defects; they do not prove complete safety or an exact process heap bound.

Cross-logon process denial requires the manual second-account procedure in the
[transport guide](windows-transport.md). Trusted methods must remain bounded:
they cannot be sandboxed or preempted. A timed-out sent mutation may have executed,
and native cancellation cleanup can exceed the request deadline. No real-time or
latency/throughput guarantee is inferred from these tests.
