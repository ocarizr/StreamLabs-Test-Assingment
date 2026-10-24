# Client API and executable demonstration

The portable future-based client composes the Windows named-pipe transport. Separate Windows
client/server processes exercise R1–R8 using the same endpoint and server instance.
No socket transport, executable upload, gRPC or client-side domain implementation
is involved.

## Run the applications

Build with the [documented presets](build.md), then start the server in PowerShell:

```powershell
./build/windows-debug/pipeforge-server.exe --serve
```

The server registers Counter and TextBuffer through the class registry and prints
`READY` only after the secured pipe instances exist. Leave that terminal open. In
another terminal run any of these commands:

```powershell
./build/windows-debug/pipeforge-client.exe --demo-sync
./build/windows-debug/pipeforge-client.exe --demo-async
./build/windows-debug/pipeforge-client.exe --demo-mixed
```

Each successful command prints `R1-R8 scenario passed` with its mode. Type `stop`,
press Enter on an empty line, or close the server's stdin to stop it gracefully.
Automated hosts must retain a writable stdin pipe until shutdown. This is a console
demonstration host, not a Windows service. No command means a usage error; it never
silently starts a server.

`--endpoint \\.\pipe\your-name` selects a local ASCII endpoint in both applications.
`--timeout-ms` accepts 10–60000 (default 5000). On the server this is the total
per-request/idle deadline; on the client it bounds each connection/call, including
queue time. `--sessions` is server-only and accepts 1–4 (default four). The demo
needs at least two slots to test isolation; two simultaneous demos need four.
Options are unique, apply only to the selected command, and are validated before
opening diagnostic files or starting IPC. See `--help` for the complete syntax.

Logging/profiling remain opt-in:

```powershell
./build/windows-debug/pipeforge-client.exe --demo-mixed --log-file build/client.log --trace-file build/client.trace.json
```

The shared diagnostics record safe request IDs and static timing categories
`client.connect`, `client.send`, and `client.receive`; peer payloads and remote error
messages are not logged. Profiling-disabled builds reject `--trace-file` as before.
The client closes and joins its workers before the diagnostic session is finalized.

## Future and blocking APIs

Public contracts live in `pipeforge/client.hpp`:

```cpp
#include "pipeforge/client.hpp"

namespace c = pipeforge::client;
namespace p = pipeforge::protocol;
namespace t = pipeforge::transport;

c::connection client(c::named_pipe_connector()); // Windows transport adapter
auto opening = client.connect_async(std::wstring(t::default_endpoint),
                                    t::clock::now() + t::default_timeout);
if (std::holds_alternative<c::connected>(opening.get())) {
    auto response = client.call_async(p::list_classes{},
                                     t::clock::now() + t::default_timeout);
    // Caller work can proceed while the response future is pending.
    auto result = response.get(); // response or a typed local failure
}
client.close();
```

`connect` and `call` wait on the exact same futures used by their async counterparts.
Async submission does not wait for a peer response; thread acquisition, validation
and bounded encoding still occur during submission. Connection attempts are
single-use: create another connection after close/failure. The adapter is injected
through a narrow framed-payload channel so portable tests can control completion
without Windows handles. Native production composition always uses named pipes.

`connect_result` contains `connected` or a local `failure`. `call_result` contains a
complete protocol response or a local failure. A structured server `remote_error`
is an ordinary correlated response, and need not close the connection. Local
categories distinguish validation, admission quota, unavailable connection,
deadline, cancellation, disconnect, access denial, protocol violation and other
transport/internal failures. Native codes remain local. Expected failures require
no exception handling around Win32 status-returning operations.

## Scheduling and correlation

A connection owns exactly three workers: one connector/serialized writer, one
reader and one deadline/cancellation monitor. At most sixteen calls are queued or
in flight; additional submissions immediately receive `quota_exceeded`. Queue entries
own their typed operation, encoded payload, promise, deadline and stop token. There
is no thread per call. Wire/codec limits still apply; these bounds are not an exact
process heap quota.

An internal monotonically increasing ID identifies each admitted request. Callers
cannot supply duplicate IDs. The reader resolves futures by ID, allowing out-of-order
replies, and rejects unknown/duplicate IDs, malformed envelopes and unexpected
outcome variants. Describe/get/create replies also match requested identity/type;
put replies preserve the supplied typed value. Method/attribute scalar interpretation
remains part of the caller's typed operation/metadata contract. The pipe server executes
one request at a time per session, while different sessions can interleave.

The independent reader is necessary for duplex backpressure: a server returning a
large response must not wait for a client that is blocked writing its next request.
The real-pipe integration test fills the full sixteen-call window with maximum-length
strings and checks all replies.

## Cancellation and lifetime

All deadlines use absolute `steady_clock` times. Before writing starts, cancellation
or expiry removes just that queued call, releases its buffer and leaves the session
usable. Once writing begins, cancellation/expiry terminates the session: that call
gets the cause, other pending calls become disconnected. The server may already
have changed state; timeout is not rollback. Requests are never automatically replayed.
Connect cancellation affects that connection attempt, not an established session.

The monitor sleeps when idle; while calls are pending it checks external stop tokens
in slices of at most five milliseconds, alongside request deadlines. A racing reply
may contend with bounded encoding/decoding and thread scheduling; those waits are
not a cancellation-latency guarantee. The reply path
rechecks cancellation/deadline before committing success. Kernel cancellation is
drained using the transport contract and may exceed the deadline; this is not a hard real-time
API. Futures can become ready before the worker finishes its drain, but operation
storage remains owned until joins.

Discarding a future does not cancel its operation. Use its stop token, deadline or
the connection controller's `close()` to request cancellation explicitly.

Call submission is thread-safe. One controller owns connect, close and destruction;
never close from a channel callback/worker. Close resolves pending promises once,
signals cancellation, joins all three workers and finally closes the channel.
Borrowed diagnostic sessions and connector captures must outlive the connection.
Allocation/custom-adapter exceptions are not disguised as native I/O failures.
An internal promise-invariant violation is fatal rather than leaving futures hanging.

## Demonstrated requirements and verification

Every mode discovers registered classes, round-trips empty/UTF-8 text, integer
boundaries and finite doubles, checks retained/paginated values and quota errors,
creates independent Counter objects, invokes methods, retrieves state/attributes,
uses TextBuffer's separately registered factory, checks invalid arguments/members,
releases an object, and verifies stale/cross-session IDs after reconnection. Async
and mixed demos submit a value batch before waiting. The process test also starts
two mixed clients simultaneously against one server and checks safe diagnostics.

Release end-to-end validation extracts the split client/server ZIPs and the bundle
and runs the same process scenarios with a system-only PATH. Their manifests remain
runtime-only. Existing unit, integration and end-to-end CTest labels remain separate;
Windows ASan also runs the process acceptance label. Linux tests cover the portable
client scheduler with controlled peers, not Win32 IPC.

See [requirement evidence and CI scope](testing.md), [Windows transport](windows-transport.md)
and [architecture](architecture.md).
