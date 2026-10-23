# M4 verification

M4 implements the Windows transport and server host described in
[the transport contract](windows-transport.md). Future-based client calls, CLI IPC
commands and the complete R1â€“R8 separate-process demonstration remain M5.

## Test-first evidence

The completion policy was first exercised against an inert backend implementation:
all four completion/configuration tests failed their behavioral assertions. The
first six real-pipe tests likewise failed against transport/host stubs. Implementing
the adapters made both groups pass. Eight additional native/security/lifecycle
tests expanded the real-pipe suite to fourteen. Temporary red-run logs and all
generated binaries stay under the ignored build tree.

## Coverage

| Layer | Executed behavior |
| --- | --- |
| Portable completion policy | Successful completion, peer failure, timeout/cancellation/wait failure, cancellation losing to completion or reporting no matching operation, mandatory terminal drain, local endpoint validation. |
| Native operations | Client connected before accept (`ERROR_PIPE_CONNECTED`), forced pending reads and backpressured writes, stop followed by terminal completion before releasing buffers/events. |
| Security | Protected pipe DACL with exactly one logon-SID allow ACE, permitted normal token, denied restricted token, occupied-name rejection and anchored instance reuse. |
| Framing and dispatch | Stateful Counter/value operations over actual byte pipes, single-byte fragmentation, concatenated frames, correlated recoverable semantic errors, malformed payload closure, zero/oversized header rejection, partial-frame deadlines. |
| Lifecycle and isolation | Busy connection deadline, unavailable endpoint/cancellation, stale-object rejection after reconnect, four concurrent sessions, shutdown with pending accepts/reads, 32 repeated lifecycle/handle-count checks, separately launched client process exiting abruptly mid-header and subsequent reconnect. |

The subprocess fixture completes a handshake before writing a partial header,
then calls `ExitProcess` to bypass normal C++ cleanup. The parent owns and reaps
the process with bounded waits. The handle-count check includes warmup and a small
allowance for lazily created runtime handles; it detects accumulation across cycles,
not every possible one-off leak. Actual cross-logon denial requires the manual
second-account procedure in the transport guide; the automated restricted-token
test is not presented as that process-level check.

## Local validation

Windows x64 validation on 2026-10-23:

- clang-cl 22.1.3 Debug: **86/86 CTest tests**, comprising 49 unit and 37 integration
  tests, including all fourteen real-pipe tests and four completion/configuration tests.
- MSVC 19.44 Release: **87/87**, additionally including isolated extraction/startup
  checks for the client, server and bundle ZIPs.
- clang-cl 22.1.3 RelWithDebInfo with whole-build ASan, frame pointers and
  `halt_on_error=1:alloc_dealloc_mismatch=1`: **86/86**, with dependencies, protoc,
  generated code, applications, subprocess fixture and tests instrumented. The same
  86/86 result was reproduced with the MSVC 2022 headers/CRT used by the CI runner.
- clang-tidy 22.1.3: **31 maintained translation units**, enabled diagnostics treated
  as errors. A vector-capacity finding in the new concurrent test was corrected.
- MSVC 19.44 Debug with profiling disabled: **79 passed, seven profiling-only tests
  skipped by design**, zero failures; all eighteen M4 completion/native cases ran.
- clang-format 19.1.5/control-spacing checks and actionlint passed.

The sanitizer configuration test checks the actual C/C++ compilation database,
excluding Windows resource-compiler commands. Configure-only dependency fixtures
verify compile/link inheritance with sanitizers disabled, Linux ASan/UBSan enabled,
and Windows LLVM runtime linkage. Windows libraries use a static CRT; CMake's
direct linker invocation receives the matching LLVM dynamic ASan runtime and
static-CRT thunk libraries explicitly.

Full Windows ASan validation exposed a nested-unwind failure while translating a
spdlog exception inside a catch handler. Moving the translated throw outside the
handler preserves the existing diagnostic error contract; the existing invalid-path
unit/process tests now pass under ASan too. No checks or test cases are suppressed.

LLVM 19 with MSVC 2022 was also tested: 84/86 passed, including all native pipe
tests, but captured C++ exception access failed in its older ASan runtime. This
configuration is therefore rejected for Windows ASan. The CI job pins official
LLVM 22.1.3 with a SHA256-verified installer; no old-runtime tests are skipped.

MSVC/ASan was investigated first: Protobuf 36.2's instrumented protoc crashed during
static initialization before schema generation. Its temporary layout opt-out also
failed and was discarded. The supported Windows sanitizer configuration therefore
uses clang-cl; normal MSVC Debug/Release support is retained. Sanitizer instrumentation
is applied before dependency creation to preserve Protobuf's ASan-dependent ABI.

## Scope of the result

The GitHub Actions Windows ASan job is configured for pull requests and master
pushes. Local execution is not a remote CI result. Linux ASan/UBSan/leak detection
and libFuzzer execution remain remote checks because this workstation has no Linux
runtime. Windows ASan does not provide those Linux checks, and handle-count tests
do not establish exact heap bounds. Trusted C++ methods cannot be preempted or
sandboxed; shutdown drains kernel completions even if that exceeds a deadline.

See [build instructions](build.md) for reproducible commands and
[the modernization plan](modernization-plan.md) for remaining milestones.
