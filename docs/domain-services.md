# Domain and application services

The domain layer implements registered server objects and bounded session-owned
storage independently of codecs and I/O. The Windows host composes its dispatcher
with named pipes; [the client guide](client.md) describes executable usage.

## Components and ownership

| Target | Responsibility | Dependencies |
| --- | --- | --- |
| `pipeforge-contracts` | Shared C++ value contracts and semantic validation | Bundled UTF-8 validator; no generated Protobuf types |
| `pipeforge-protocol` | Payload codecs and framing | Contracts, generated code and Protobuf runtime |
| `pipeforge-domain` | Registry/factories, session-owned repositories, built-in behavior | Contracts only |
| `pipeforge-services` | Correlated request dispatch, codec-error translation, optional safe diagnostics | Domain and diagnostics |

`contracts.hpp` preserves the existing `pipeforge::protocol` type names. `protocol.hpp` includes it for source compatibility. Shared identifier/kind validation moved out of the codec so direct domain calls and wire calls follow the same rules. Names are bounded ASCII identifiers; text values preserve valid UTF-8, embedded NULs and empty strings. Numeric values preserve their variant type; doubles must be finite.

The server composition owns one startup registry and one `identifier_source`, then creates one `session_store` and `server::dispatcher` per session. Registry and ID source outlive every store; stores outlive their dispatchers. Register every class before creating stores and do not mutate, move or replace that registry while stores exist. These objects have no internal synchronization: the pipe host must serialize all domain access across sessions, including ID allocation and disconnect cleanup.

Each store owns values and `unique_ptr<object>` instances. `close()` is idempotent, destroys all state and makes further operations return `access_denied`. Destruction also releases all instances. Object IDs and value IDs share a server-lifetime monotonic sequence. IDs are nonzero, never reused and exhaust without wrapping; they are predictable identifiers, not authorization credentials. Lookups are restricted to the receiving session's repositories. A foreign, released, stale or wrong-resource ID returns `not_found` without revealing another session's state.

## Registration and invocation

Trusted server code calls `class_registry::add(class_definition)` and handles its optional typed error before accepting sessions. Definitions contain a class description, a factory and a declared state bound. Registration rejects empty factories, duplicate class names, invalid or duplicate metadata names/parameters, unsupported kinds, oversized cardinalities, excessive aggregate schema nodes and invalid state bounds. The registry holds at most 64 sorted entries.

Factories receive validated named constructor arguments and return an owned object or a typed error. Parameter sets must match exactly by name and kind, irrespective of ordering; there are no implicit conversions or default arguments. A custom class implements only `object::clone`, `state` and `invoke`. No dispatcher or transport changes are necessary. `state()` must report exactly the registered attribute set and kinds. Registered methods likewise have validated names, argument sets and result kinds.

| Built-in | Constructor | Attribute | Method |
| --- | --- | --- | --- |
| `Counter` | `initial: integer` | `value: integer` | `increment(delta: integer) -> integer` |
| `TextBuffer` | `initial: text` | `text: text` | `append(suffix: text) -> text` |

An attribute's `read_only` flag forbids direct assignment; methods can update the underlying state. There is no direct set-attribute operation in v1. Counter checks both signed overflow directions before adding, including extreme deltas. TextBuffer checks remaining text capacity before appending.

Invocation runs on an independent deep clone. Only after successful invocation, result/state validation and shared quota checks does the store replace the original object and update accounting. Returned failures discard the candidate. This provides atomic domain-state updates for expected status errors, including an implementation that mutates its clone before returning failure. Clone/snapshot copies trade bounded additional memory and CPU for straightforward rollback; they are not a throughput optimization.

Factories and custom implementations are trusted linked C++, not a sandbox. They must perform bounded work, keep bounded owned state, avoid blocking I/O/external side effects, return independent clones, and release resources without throwing. Arbitrary hidden allocations and external effects cannot be rolled back or bounded by snapshot inspection. Unexpected exceptions, including allocation failures, propagate; ordinary validation and quota failures use typed results. No broad exception handlers were added.

## Quotas and pagination

Defaults and hard ceilings are 100 values, 100 objects and 8 MiB of accounted state per session. Custom limits can only tighten these ceilings; oversized settings are clamped and zero disables that storage resource. A class's declared state bound is nonzero and at most 512 KiB. The protocol still limits each string to 64 KiB and each collection to 64 entries.

Accounting is deterministic logical accounting, not exact heap measurement:

- A value record costs 128 bytes plus UTF-8 text length or 8 bytes for an integer/double.
- An object costs 128 bytes plus its class-name length and each state's named-value charge (128 bytes plus name length plus value payload charge).
- Values and objects consume the same session byte budget. Releasing an object returns its charge; growth is checked against the budget before publishing the clone.

Snapshots/replies and temporary clones are additional bounded transient data. Factory execution, allocator overhead, executable code and shared registry metadata are outside session accounting. A host must also bound session count and pending work; domain limits do not establish a process-wide heap quota.

Values are sorted by ID and classes by name. Cursors are exclusive keys: zero/empty begins a list, unknown well-formed keys seek forward, and zero/empty `next` ends it. A nonterminal cursor is the last returned key. Pages are not snapshots across mutations. Value pages also stop at a conservative 512 KiB logical charge, even when the requested item limit is larger, so maximum-sized text values cannot create an oversized 1 MiB response. Every stored single value fits that page budget.

## Service errors and observability

`dispatcher::dispatch` accepts a typed v1 request and returns a response with the original ID/current supported version, or a fatal protocol failure when the ID is zero. Unsupported versions do not execute domain operations. `reject` translates codec `invalid_fields`, `unsupported_version` and `limit_exceeded` to correlated domain errors only when the nonzero ID is trusted and the failure permits continuation. Malformed, truncated, uncorrelated and close-session failures remain fatal for the future transport to close.

Unknown IDs/classes/members return `not_found`; wrong arguments and malformed direct values return `invalid_request`; quota/ID exhaustion returns `quota_exceeded`; Counter overflow returns `numeric_overflow`. Null factories/clones and invalid implementation output return `internal_failure`. Error text comes from fixed local categories, including when a factory/method supplies its own error text.

Diagnostics remain opt-in through the existing `diagnostic_session`. Dispatch logs only the correlation ID, a locally defined operation name and numeric result category. It never logs class/member names, argument values, snapshots or factory error text. Scope labels are fixed `server.dispatch`. Profiling-disabled builds retain logs and create no trace file. See [the requirement-to-test mapping](testing.md) for coverage and its limits.
