# M5 verification

M5 implements [the client API and executable demonstrations](client.md). The
client links the shared contracts, codec, diagnostics and transport; server domain
services remain in the server executable. Production communication uses M4's
secured local Windows named pipes.

## Test-first evidence

Five initial controlled-peer client tests compiled against an inert implementation
and failed their behavioral assertions before the scheduler was implemented. All
five then passed. CLI tests likewise exposed three expected failures before command
parsing/dispatch was added. The first process acceptance run failed because the old
server rejected `--serve`; the completed executables pass that same scenario.
Additional cancellation, malformed-peer, concurrency and native lifecycle cases
expanded coverage. Temporary red/green logs remain in the ignored build tree.

## Requirement coverage

| Requirement | Executed acceptance behavior |
| --- | --- |
| R1 | Separate client/server processes connect to one local named-pipe endpoint and shut down cleanly. No socket fallback. |
| R2 | Blocking and future-based calls share implementation; controlled pending connect/write/reply gates prove that caller work can proceed before completion. Reversed replies resolve the correct futures. |
| R3 | Each process mode round-trips empty and UTF-8 strings, int64 minimum/maximum and finite doubles with preserved types. Existing M2 validation tests retain malformed/nonfinite/overflow coverage. |
| R4 | Counter construction, attribute access, method invocation and later object retrieval demonstrate retained server state. Invalid members and arguments return structured errors. |
| R5 | Sync, async and mixed demos run against the same server without restarting it; two mixed clients run simultaneously. Both connection APIs are exercised. |
| R6 | Values remain retrievable by ID and through paginated enumeration. Quota errors preserve the connection; another session cannot retrieve the original session's values. |
| R7 | The server registers Counter and TextBuffer factories. Clients discover their metadata and exercise both types. Existing M3 tests cover duplicate registration and registry extensibility. |
| R8 | Independent Counter instances retain independent state. Release, cross-session access and reconnect reject stale IDs; existing M3 destruction probes cover actual object destruction. |

Controlled-peer tests also cover unknown/duplicate IDs, wrong outcome variants,
malformed responses, peer exit, sixteen-call admission limits, concurrent submitters,
queued timeout/cancellation, sent timeout/cancellation, connection cancellation,
shutdown and repeated close. Completion is protected by the same mutex as pending
state, including reply/timeout races. No automatic retry can duplicate a mutation.

Three new native client tests exercise the full sixteen-call window with
maximum-length strings, peer/client shutdown with a pending read, and 32 repeated
client/server lifecycles with a warmed-up process handle-count check. The latter
allows four incidental runtime handles and detects accumulation, not every possible
one-off leak. These complement M4's native cancellation/drain and security tests.

The process harness owns and reaps its children with bounded waits and unique
endpoints. It checks all three modes, simultaneous mixed sessions, unavailable
endpoints, safe logs and client timing categories. Release acceptance repeats the
same scenarios from extracted client/server ZIPs and the combined bundle, using a
system-only PATH and fresh working directories.

## Local validation

Windows x64 validation on 2026-10-23:

- clang-cl 22.1.3 Debug: **101/101 CTest tests**, comprising 60 unit, 40 integration
  and one process end-to-end test.
- MSVC 19.44 Release: **102/102**, adding the runtime-package end-to-end test with
  the extracted split and bundled distributions.
- clang-cl 22.1.3 RelWithDebInfo, MSVC 2022 headers/CRT and whole-build ASan:
  **101/101**, including native/process acceptance, with
  `halt_on_error=1:alloc_dealloc_mismatch=1`. Dependencies, generated code,
  applications and tests are instrumented together.
- MSVC 19.44 Debug with profiling disabled: **94 passed, seven profiling-only tests
  skipped by design**, zero failures. All client/native/process scenarios ran.
- clang-tidy 22.1.3: **37 maintained translation units**, with all enabled project
  diagnostics treated as errors and no failing files.
- clang-format 19.1.5, supplemental control/proto spacing, actionlint and
  `git diff --check` passed.

clang-tidy findings were corrected in checked indexing, parser iterators and class
lifetime contracts. The internal exactly-once promise completion helper has one
documented exception-escape suppression: a violated promise invariant is fatal,
rather than translated into a misleading I/O result. No analyzer checks are
disabled for the client component.

## Scope and remaining work

The client owns three workers and admits at most sixteen calls; the codec and
transport retain their existing limits. These are count/payload bounds, not an
exact heap quota. External cancellation is observed in bounded polling slices
while calls are pending, subject to scheduling and bounded codec work. Kernel I/O
must drain before storage is released, even if cleanup exceeds a deadline. A sent
mutation may already have executed when its caller times out.

Windows ASan CI now includes the process end-to-end label. Local results do not
establish a successful remote workflow run. This workstation has no Linux runtime;
Linux ASan/UBSan/leak detection and fuzzing remain CI checks. Linux TSan coverage
for the portable scheduler and generated Doxygen documentation remain in M6's
final quality/delivery audit. The console server uses stdin for graceful shutdown;
Windows service integration and cross-logon manual security checks remain outside
this milestone.

See [build instructions](build.md), [release validation](releases.md) and
[the modernization plan](modernization-plan.md).
