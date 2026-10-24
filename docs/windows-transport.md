# Windows named-pipe transport

M4 provides a Windows x64 transport library and bounded server host. The public
interfaces are `pipeforge/transport.hpp` and `pipeforge/pipe_server.hpp`. M5 provides [future-based client calls and executable commands](client.md).

## Ownership and dependencies

`pipeforge-transport-core` contains portable endpoint validation and the completion
policy. `pipeforge-transport` adds Windows byte streams and depends on the shared
protocol library, without importing domain/services into clients.
`pipeforge-pipe-server` combines that transport with the M3 dispatcher. The server
executable composes the host through its `--serve` command.

The host owns a frozen startup registry, process-wide object IDs, one worker per
pipe slot, and a shared stop source. There are 1–4 slots (default four). All secure
instances exist before `start()` returns. A single domain gate serializes service
dispatch and session cleanup, preserving M3's ownership and accounting invariants.
Class implementations are trusted, bounded C++ code with no blocking I/O; exceptions
from custom implementations or allocation failure are not translated into peer
errors. The only new catch handles OS thread-creation failure and joins any workers
already launched.

A move-only `pipe_stream` allows one reader and one serialized writer concurrently.
Its owner must finish outstanding calls before moving, closing or destroying it.
`native_handle()` is borrowed interop access and never transfers ownership. Host
pipe handles remain anchored across disconnects; the first instance is released
only after shutdown has joined all workers. The diagnostic session, when supplied,
must outlive the host.

## Security policy

Names must begin with `\\.\pipe\` and contain a nonempty ASCII component using
letters, digits, `_`, `-` or `.`; total length is at most 240 characters. UNC remote
paths and nested components are rejected. The default is `\\.\pipe\pipeforge-v1`.

The protected DACL contains one allow ACE for the process token's logon SID.
Obtaining or converting that SID/descriptor must succeed; there is no default-DACL
fallback. All instances use `PIPE_REJECT_REMOTE_CLIENTS`. The first also uses
`FILE_FLAG_FIRST_PIPE_INSTANCE`, so an occupied name fails startup. Handles are
non-inheritable. Clients request `SECURITY_IDENTIFICATION` to prevent a connected
server from using an impersonation-level client token.

This is a same-logon, non-elevated desktop boundary. Another process in that logon
can use the endpoint and is not strongly authenticated. This is unsuitable for a
privileged service or a compromised account. Windows ACL owners/administrators can
change security descriptors; no stronger claim is made.

The automated security test inspects the actual pipe DACL and uses `AccessCheck`
with both the normal token and a restricted token lacking an allowed restricting
SID. An actual other-logon process remains a manual check: start the host under
account A, attempt `pipe_stream::connect` from a separate logon/account B, require
`access_denied`, then verify account A can still exchange requests. Remote access
is rejected by name validation and the server's native creation flag.

See [Microsoft's pipe security model](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights)
and [CreateFile security quality of service](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew).

## Framing, limits and errors

The M2 decoder handles split headers/payloads and preserves concatenated frames.
Lengths are checked before payload allocation: 1 MiB maximum payload, with the
existing codec depth/cardinality/string limits. Native transfers use 16 KiB chunks.
At most four sessions decode concurrently. Each session executes one request and
response at a time, a stricter bound than the plan's sixteen outstanding requests.
There is no application request queue; kernel buffering applies backpressure.
These limits bound concurrency and wire sizes, not exact process heap usage.

Fatal framing, truncation and uncorrelatable parse failures close the session.
Decodable semantic errors use M3's correlated structured response when permitted,
and the session can continue. Sequential reuse of an ID is valid once the prior
response finishes; M5 owns client-side outstanding-ID correlation. No transport
operation automatically replays a mutation.

Every stream I/O/framing failure is terminal, including timeout/cancellation.
Native errors remain local typed failures; logs contain only local categories,
codes and the dispatcher's safe correlation metadata, never peer payloads.
An error after dispatch may mean a mutation completed without a delivered reply.

## Deadlines, cancellation and shutdown

The stream API uses absolute `steady_clock` deadlines and optional stop tokens.
Busy/unavailable connection attempts retry with waits of at most 50 ms, including
cancellable backoff for absent names. No spin loop or unbounded retry is used.
Host request timeout defaults to 5 seconds and accepts 10 ms–60 seconds. It covers
idle/frame receipt, domain-gate acquisition, dispatch and response transmission;
an idle session therefore expires too. Trusted running methods cannot be forcibly
preempted. Cancellation cleanup can exceed a deadline while kernel completion is
drained; this API does not promise hard real-time completion.

Each native operation owns zero-initialized `OVERLAPPED` storage, manual-reset
completion/cancellation events and its stop callback. Immediate success,
`ERROR_IO_PENDING` and the `ERROR_PIPE_CONNECTED` accept race are separate paths.
On timeout/cancellation/wait failure, `CancelIoEx` is followed by terminal
`GetOverlappedResult`, even if cancellation reports `ERROR_NOT_FOUND` or races with
successful completion. Buffers, events and operation storage remain alive until
that drain completes. A destructor supplies the same safety fallback for future
unwinding paths. Completion takes precedence if both wait events are signaled.

`request_stop()` is a thread-safe signal. A single controller calls `stop()`
(never a worker); it signals all operations, joins every worker, releases all
session objects and then closes the anchored instances. Repeated `stop()` is safe.
Concurrent `stop()` calls or destruction during external calls are unsupported.
Statistics are approximate concurrent snapshots and become stable after `stop()`.

This follows Microsoft's contracts for [ConnectNamedPipe](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-connectnamedpipe),
[CancelIoEx](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex)
and [GetOverlappedResult](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getoverlappedresult).

## Verification

The unit label includes a controllable completion backend covering cancellation
races and drain ordering. Windows integration tests cover real pipe framing,
stateful dispatch, security, connection races, pending read/write cancellation,
busy endpoints, deadlines, concurrent sessions, abrupt process exit, anchored-name
reuse and repeated handle-count checks. See [M4 verification](m4-verification.md)
for executed results and remaining platform checks.
