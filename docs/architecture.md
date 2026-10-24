# Architecture and contracts

PipeForge separates value contracts, serialization, native I/O, client scheduling
and server behavior. The two executables compose static libraries through CMake;
the client does not link server domain/services code.

| Target | Responsibility and dependencies |
| --- | --- |
| `pipeforge-contracts` | Owning values and shared semantic validation; no codec or platform dependency. |
| `pipeforge-protocol` | Protobuf conversion and framing; depends on contracts and the pinned generated/runtime code. |
| `pipeforge-transport-core` | Portable completion policy and endpoint validation. |
| `pipeforge-transport` | Windows byte-mode pipe adapter with RAII native operations and framing. |
| `pipeforge-client-api` | Bounded queue, promises, correlation and deadlines; injected channel, Windows adapter for production. |
| `pipeforge-domain` | Registry, factories, state and session repositories; depends only on shared contracts. |
| `pipeforge-services` | Correlated dispatcher and safe optional diagnostics around domain execution. |
| `pipeforge-pipe-server` | Secure bounded pipe slots and serialized domain access. |
| `pipeforge-diagnostics` | Explicit spdlog sinks and bounded scope profiling, with no global logger. |
| CLI command libraries | Role-specific composition, with thin separate executable entry points. |

## Ownership and concurrency

A server owns one frozen startup registry and one nonwrapping ID source. Each
accepted session owns its repositories and dispatcher. One host gate serializes
domain dispatch and disconnect cleanup across sessions; registered methods perform
bounded work without blocking I/O. At most four workers/sessions exist.

A client connection owns one serialized writer/connector, one reader and one
deadline/cancellation monitor. Sixteen calls may be queued/in flight. Each entry
owns its typed operation, encoded bytes, promise, deadline and stop token. Blocking
methods wait on the same futures used by async methods. One controller owns
connect/close/destruction; callers may submit calls concurrently.

Native operations own their initialized OVERLAPPED state, events and buffers until
terminal completion is observed. Cancellation requests do not release that
storage. Shutdown signals cancellation, drains/joins workers and only then closes
the channel. Futures may become ready before kernel cleanup finishes.

## Wire and error policy

Each local duplex stream carries a four-byte little-endian length followed by a
bounded Protobuf envelope. Nonzero monotonically generated request IDs correlate
responses. Unknown/duplicate IDs, wrong outcome variants and malformed framing
terminate the client session. A correlated semantic remote error may leave it
usable. No C++ object layout, pointer, handle or executable code crosses the pipe.

Expected failures use typed variants, with stable remote codes and local-only
native details. Win32 status returns are checked directly. Specific exceptions
from throwing file adapters are translated at their I/O boundary; allocation and
trusted custom-code failures are not disguised as recoverable native statuses.

Queued cancellation removes only that unsent call. Once writing starts, timeout
or cancellation terminates the session because execution may be indeterminate.
There is no automatic reconnect, mutation replay, persistence or restart recovery.

## Security and resource scope

The host is a non-elevated local desktop process. A protected DACL allows the
server token's logon SID and rejects remote clients. The first pipe instance
anchors endpoint ownership. This does not authenticate against a compromised
account or a malicious process with the same logon rights.

Parser limits, four sessions, sixteen pending client calls and per-session logical
storage budgets bound separate resources. A 1 MiB frame limit is not an exact
decoded heap limit, and logical storage accounting excludes allocator overhead,
temporary clones and executable state. See [protocol limits](protocol.md),
[domain quotas](domain-services.md), [client lifecycle](client.md),
[native transport](windows-transport.md) and [test evidence](testing.md).
