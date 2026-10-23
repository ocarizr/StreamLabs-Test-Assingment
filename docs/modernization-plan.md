# PipeForge modernization plan

Status: proposed implementation baseline. Date: 2026-10-23.

This stage reviews the repository and defines the work. It does not implement the replacement application, tests, CMake migration, or GitHub Actions workflows. R1-R8 refer to the numbered functional requirements in the README. These decisions replace the legacy design choices and assumptions.

## 1. Source-review findings

These are source observations, not results of executing the legacy binaries.

| Finding and evidence | Consequence | Planned response |
| --- | --- | --- |
| `Server/Server.cpp` reads directly into maps and writes them using their addresses and `sizeof`; `Client/DefaultConnection.cpp::Push` similarly writes a pair containing a string. | STL ownership metadata and pointers are not transferable values. Incoming bytes can corrupt object internals. | Encode values into bounded byte buffers; never transmit C++ object representations, pointers, or handles. |
| `Server/Server.cpp` duplicates dispatch and storage across sync/async loops. | Fixes can diverge and behavior is hard to isolate. | One dispatcher and application service independent of I/O completion style. |
| Several `OVERLAPPED` structures are uninitialized; loops spin; the async server calls `vDisconnectAndConnect(hAsync, false)`. | Invalid operation state, excessive CPU use, and inconsistent connection handling. | Initialized, owned operation state; explicit completion/cancellation; bounded waits. |
| `Client/Client.cpp` uses `DefaultConnection("///.//pipe")`, while the server creates `pSyncPipe` and `pAsyncPipe`; most former client operations are commented out. | No coherent source-level demonstration of the requirements. | One configured endpoint and CLI scenarios covering every requirement. |
| `Client/DefaultConnection.h` declares `IsConnected() noexcept`, but its definition omits that specification; the client CMake file omits `DefaultConnection.cpp`. | Source declarations and build definitions need repair or replacement. | Root target-based CMake build, validated from a clean checkout. |
| `Client/Utilities.h` catches all exceptions and reduces failures to booleans, including around non-throwing Win32 APIs. | Failure context disappears; exceptions do not replace native result checking. | Typed error categories and retained native codes; narrow exception translation. |
| Client ownership uses raw `new`; `IConnection` has no virtual destructor; handle cleanup depends on control flow. | Leaks and unsafe polymorphic destruction are possible. | RAII, value ownership, virtual destructors where polymorphic deletion is required. |
| Text/number output streams do not request append; paths incorporate class/object names; list handlers assign integer counts to strings. | Values can be overwritten, paths are uncontrolled, counts are not decimal text. | Typed storage and validation; no filesystem naming in wire requests. |
| The old design downloads source, compiles via shell commands, and loads code on the client; that main path is currently commented out. | Does not establish server-side object invocation and introduces code-execution/toolchain coupling. | Trusted server registry and invocation through object IDs. |
| `.vs`, `Debug`, and `VS Build` outputs are tracked; no automated tests or workflows exist. | Machine-specific artifacts obscure reproducibility. | Ignore rules now; untrack generated artifacts during migration. Ignore rules alone do not untrack files. |

## 2. Requirements and acceptance criteria

| Requirement | Intended behavior | Acceptance evidence |
| --- | --- | --- |
| R1: named-pipe connection | Separate processes connect over a local Windows named pipe. | Start server/client processes, exchange a versioned request, close cleanly. No socket fallback. |
| R2: sync and async calls | `call` waits; `call_async` returns a future without waiting for the response. | Controlled pending response keeps the future pending while caller work proceeds; both APIs return equivalent results/errors. |
| R3: trivial data | Preserve UTF-8 strings, signed int64 values, and finite doubles with explicit types. | Cross-process round trips for empty/non-ASCII strings, integer boundaries, floating values; reject malformed encodings, overflow, and nonfinite values. |
| R4: remote objects | Create server objects, retrieve identity/state, read attributes, and invoke methods on the server. | A `Counter` exposes initial value, `value`, and `increment(delta)`; later retrieval proves retained server state. Unknown IDs/members and wrong arguments fail predictably. |
| R5: sync/async connections | One running server accepts blocking/nonblocking connection APIs and both call styles. | Connect both ways and exercise mixed calls with concurrent sessions without restarting or selecting a server mode. |
| R6: data storage | Bounded session repositories retain typed values with IDs and paginated retrieval. | Multiple writes remain retrievable; verify type identity, pagination, quotas, and isolation. |
| R7: custom class registration | Server application code registers a C++ factory with attribute/method metadata, discoverable by clients. | Add a second test type without modifying dispatcher/transport; reject duplicate registrations and unknown classes. |
| R8: object storage | Server owns and retains created instances in a session object store. | Independent instances preserve independent state; release/disconnect destroys them; stale/cross-session IDs fail. |

R7 does not specify who initiates registration or require uploading source. The baseline is trusted server-code registration at startup. Client-defined executable behavior would be a separate feature with a different isolation model. Retrieving an object means retrieving its ID, type, and serialized state, never its C++ memory layout.

## 3. Replacement design choices

| Legacy decision/assumption | New decision | Rationale and tradeoff |
| --- | --- | --- |
| One large source file per application | Components by responsibility, two thin entry points | Test domain behavior without Windows handles; requires explicit component contracts. |
| Solution as primary build entry point | CMake 3.25+ baseline, Ninja presets, C++20, Windows x64 | IDE-independent orchestration; Windows SDK and compatible compiler remain necessary. Confirm exact versions during implementation. |
| Separate sync/async server modes | One overlapped transport; blocking wrappers wait on the same operations | Shared behavior; operation lifetime/shutdown require careful tests. |
| Files/folders now; database assumed preferable | Bounded in-memory storage | Requirements need storage, not durability. Avoid unnecessary recovery/consistency obligations. |
| Client compiles and loads custom classes | Server registry with factories and metadata | Objects and methods remain server-side; adding trusted classes requires rebuilding the server application. |
| Arbitrary class/object files | IDs and validated typed values | Processes share value contracts, not executable code or ABI. |
| One client and one mode per run | Bounded concurrent sessions with mixed call styles | Removes unnecessary restriction; initial domain mutations are serialized. |
| Positional maps and short opcodes | Versioned fields, explicit operation names, IDs, and errors | Testable validation and evolution; legacy wire compatibility is dropped. |
| Catch-all helpers | Native result checking and targeted exception translation | Preserve failure causes; do not treat native status codes as thrown exceptions. |

### Components and dependencies

Proposed layout, to be created during implementation:

```text
CMakeLists.txt / CMakePresets.json
include/pipeforge/          Public value contracts and client API
src/protocol/              Serialization, framing, validation
src/domain/                Registry, values, object store
src/server/                Application services and request dispatch
src/client/                Request correlation, blocking/future APIs
src/transport/windows/     Pipes, access control, operation lifetime
apps/client/               CLI composition and examples
apps/server/               Registration, configuration, shutdown
tests/unit/                Protocol/domain/application/client tests
tests/integration/         Real Windows pipe and process tests
tests/fuzz/                Decoder harness and regression corpus
docs/                      Architecture, protocol, usage, Doxygen
.github/workflows/         Build, analysis, sanitizers, documentation
```

Use `PipeForge` as the CMake project name and `pipeforge` as the public C++ namespace. The future executable targets are `pipeforge-client` and `pipeforge-server`.

Domain code has no transport/client/Win32 dependencies. Protocol code depends on value contracts; transport moves bytes without interpreting business operations. Server services depend on narrow repository contracts and the class registry. Entry points compose implementations. Client links contracts and transport, not server implementation.

Apply SOLID through separate responsibilities, extensibility by registration, consistent lifetime/error contracts, small interfaces, and injected I/O dependencies. Apply DRY to codecs, validation, dispatch, and error translation. Avoid an interface for every class or abstractions for hypothetical backends.

Use Factory/Registry for classes, Repository for stored state, Adapter for Win32 I/O, and RAII for ownership. Prefer a dispatcher table over a hierarchy for every command. Each pattern must solve a concrete coupling or lifecycle problem.

### Wire contract

- One local duplex endpoint, initially `\\.\pipe\pipeforge-v1`. Tests use unique names and explicit readiness signals.
- Byte-mode pipe with a 4-byte unsigned little-endian payload length followed by UTF-8 JSON. Request envelope: `version`, `request_id`, `operation`, `arguments`. Response: echoed ID plus either `result` or `error`.
- Initially use nlohmann/json, pinned to a reviewed release at implementation time. Validate schemas without implicit coercion; reject duplicate keys and unknown envelope fields. Enforce nesting bounds before constructing an unbounded object tree. Do not hand-roll JSON parsing.
- Values carry explicit type tags. Encode int64 values as canonical decimal strings to prevent precision loss in JSON consumers. Specify finite-double round trips, UTF-8 validity, and embedded-NUL handling in tests.
- Validate lengths before allocation; handle partial transfers and concatenated frames. Never assume one read equals one request or transmit an STL representation.
- Initial defaults: 1 MiB frame payload, nesting depth 16, 64 KiB strings, 4 sessions, 16 pending requests per session, 100 stored values and 100 objects per session, 8 MiB accounted session state. Registry types declare bounded state/results. Reject mutations before quotas are exceeded; paginate list responses to stay within frame limits.
- Connection/request deadlines default to 5 seconds, configurable within documented bounds. Bound queues/workers; overload returns a stable error.
- Operations: describe/list classes, put/get/list values, create/get/release object, get attribute, invoke method. Specify schemas, numeric-overflow behavior, and error codes before implementation.

### Concurrency, lifetime, and errors

The server accepts overlapped connections and maintains one read and one serialized write stream per session. One bounded dispatcher serializes domain operations, avoiding concurrent object mutation initially. Registered methods must be bounded and perform no blocking I/O. This is a small concurrency model, not a throughput claim.

The client owns a bounded I/O worker and correlates promises by request ID. `connect_async` and `call_async` return futures; blocking APIs wait on those same operations. Every pending promise resolves exactly once, including timeout, cancellation, disconnection, and shutdown.

Each operation owns its buffer, initialized `OVERLAPPED`, and event until completion is observed, including after `CancelIoEx`. Requesting cancellation does not authorize destroying operation state. Destructors release resources without throwing.

Objects and values belong to their creating session, survive until explicit release/disconnect, and are inaccessible from other sessions. IDs are opaque; possession alone does not authorize cross-session access. Reconnection starts a new session. Do not transparently replay mutations: a timeout can leave execution indeterminate and does not guarantee rollback.

Expected failures use a typed result, implemented with `std::variant` in C++20. Categories include invalid request, not found, quota exceeded, timeout, cancelled, access denied, transport failure, and internal failure. Capture native codes immediately after failed calls. Wire errors expose stable codes; sensitive diagnostics stay local.

Check Win32 return values directly. Catch specific exceptions from throwing parser/filesystem/stream adapters at their boundary and translate them. Keep ordinary validation exception-free. JSON decoding exception translation belongs at the input boundary. Avoid blanket catch-and-continue helpers and pervasive try/catch blocks.

## 4. Security and IPC failure model

Treat local peers as untrusted input. The baseline is a same-user, non-elevated desktop server, not a privileged service or public network service.

- Use an explicit DACL restricted to the intended user/logon context; reject remote clients using `PIPE_REJECT_REMOTE_CLIENTS`. Test other-user denial where runner permissions allow, and document manual checks otherwise.
- Use `FILE_FLAG_FIRST_PIPE_INSTANCE` for the first listener instance to fail on an occupied name. Later instances belong to that listener. Test this lifecycle. Same-user malicious processes remain outside a strong authentication boundary; do not claim protection against a compromised account.
- Handle immediate/pending completion, `ERROR_IO_PENDING`, `ERROR_PIPE_CONNECTED`, busy/unavailable pipe, zero-byte EOF, peer exit, truncated frames, failed writes, cancellation races, and shutdown. Avoid busy spins, unconditional flushes, and unbounded retries.
- Reject malformed JSON/UTF-8, unsupported versions, duplicate or unknown request IDs, invalid fields/members/numbers, oversized lengths, excessive nesting, and quota exhaustion. Framing violations close the session; valid frames containing invalid operations receive structured errors.
- Accept no source uploads, library paths, filesystem paths, shell commands, function pointers, or executable code. Remove dynamic code loading.
- Do not log payloads by default. Log operation, correlation ID, error category, and native code where useful.

Static analysis does not prove memory or IPC safety. Combine ownership design, bounded parsing, failure injection, sanitizers, fuzzing, and real transport tests. Document residual limitations.

## 5. TDD and verification

Use GoogleTest and CTest, pinned when introduced. Unit tests inject fake byte transports and a controllable clock/completion scheduler. Integration tests use actual Windows named pipes. Assert observable behavior and cleanup rather than mirroring implementation details.

For every slice: write a failing behavioral test, verify its intended failure, implement the smallest passing behavior, then refactor with tests green. Record brief red/green evidence in the work summary; commits remain manual. An unbuildable legacy baseline is not evidence of a meaningful failing behavioral test.

| Layer | Required cases |
| --- | --- |
| Protocol | Independent known wire vectors, round trips, split headers/payloads, concatenated frames, overflow/oversize lengths, duplicate keys, nesting, malformed UTF-8/JSON, numeric boundaries. |
| Domain | Second registered class, metadata, duplicate registration, factory failure, stateful methods, wrong arguments, overflow without mutation, independent instances, stale IDs, ownership, quotas. |
| Application/client | Dispatch, typed errors, sync/async parity, out-of-order correlation, unknown response IDs, backpressure, deadlines/cancellation races, exactly-once promise completion. |
| Transport | Immediate/pending connect and I/O, connect race, short transfers, disconnect mid-frame, unavailable/busy pipe, timeout, cancellation followed by completion, pending-operation shutdown. |
| End to end | Separate processes satisfy R1-R8; mixed-style sessions; isolation; repeated connect/disconnect and resource-count checks for monotonic leaks. |

Use readiness/completion signals and test deadlines instead of timing-sensitive sleeps. Capture failure diagnostics and always reap subprocesses. Collect coverage to locate omissions; no percentage substitutes for the required behavioral cases.

## 6. Build, CI, and API documentation

Use target-scoped language standards, warnings, include paths, dependencies, and sanitizer flags. Provide configure/build/test presets and clean-checkout CLI instructions. Pin test/JSON dependencies by revision/hash and document an offline cache path. Do not commit downloads or binaries. Export a real compilation database using Ninja/Clang for clang-tidy.

| Required CI job | Checks |
| --- | --- |
| Windows x64 MSVC Debug + Release | CMake/Ninja build of both executables, strong project warnings as errors, unit and actual pipe/process tests through CTest. |
| Windows x64 Clang | clang-cl with Windows SDK, clang-tidy across all project translation units using the compilation database, unit/integration tests. |
| Formatting | Pinned clang-format checks project headers/sources without modifying the checkout. Commit formatting and tidy configurations. |
| Windows AddressSanitizer | Compatible separate configuration runs unit and real pipe/process tests; preserve crash reports. Verify supported flag combinations first. |
| Linux portable core | Protocol/domain/application tests under ASan+UBSan; separate TSan configuration for portable concurrent client/session logic. Does not validate Win32 transport. |
| Decoder fuzzing | Bounded libFuzzer PR smoke run and longer scheduled run on the portable decoder with ASan/UBSan; retain minimized regression inputs. |
| Documentation | Doxygen generation fails on generation warnings, missing documented public APIs, and supported reference checks; upload generated HTML as an artifact. |

ASan and TSan run separately. Do not promise Windows TSan coverage: Windows is absent from its supported-platform list. Pin or record exact tool versions and runner images during implementation.

Validate pull requests and pushes; scheduled fuzzing complements these checks. Use job timeouts, minimal permissions, dependency caches keyed by pinned inputs, and actions pinned to reviewed commit SHAs. Untrusted pull requests receive no publishing credentials. Never execute contribution code with write permissions through `pull_request_target`.

Document public interfaces with Doxygen comments describing purpose, parameters, returns/errors, ownership, thread safety, cancellation, and side effects. Explain non-obvious protocol/security/lifetime choices near code. Avoid comments that merely restate statements. Keep code and documentation in English for consistency across the project.

Generated HTML artifacts are the initial deliverable. GitHub Pages is the preferred optional published API reference. A wiki summary is optional: confirm wiki availability and authentication, generate into a temporary checkout, and update generated pages only from trusted branch builds or manual dispatch. Do not assume `GITHUB_TOKEN` can write to the separate wiki repository. Publishing setup must not block required build/test validation.

## 7. Implementation milestones

| Milestone | Work and test-first focus | Exit criteria |
| --- | --- | --- |
| M0: planning, this change | Audit, requirement mapping, replacement decisions, ignore rules, remove `README.docx`. | Reviewable plan and explicit scope; stage intended files without committing. |
| M1: build and test harness | Root CMake/presets, minimal separate executable targets, GoogleTest/CTest, warnings, formatting, initial build CI. Remove solution/project/PCH dependencies as replacement targets become viable; untrack generated `.vs`, `Debug`, and `VS Build` outputs. | Clean checkout builds/tests without an IDE; generated outputs are untracked. Do not claim feature parity yet. |
| M2: protocol | Version-1 schemas and errors; malformed-input/framing tests before implementation; JSON dependency, tidy, sanitizers, fuzz harness. | Bounds enforced before unbounded allocation; protocol tests and fuzz smoke pass. |
| M3: domain/services | Typed repositories, registry/factories/metadata, Counter and second test type, object lifecycle, dispatcher. | In-process R3/R4/R6/R7/R8 tests, including ownership/quota failures. |
| M4: Windows IPC | RAII wrappers, DACL, framing, overlapped accept/read/write, deadlines, cancellation, bounded sessions, shutdown. | Real transport/failure tests and pending-operation cleanup pass under ASan. |
| M5: client and demonstration | Future-based connect/call, blocking wrappers, CLI, process tests. | Separate processes satisfy R1-R8, including simultaneous mixed-style sessions; superseded legacy code removed. |
| M6: quality/delivery | Complete CI matrix, usage/protocol/Doxygen guides, optional publishing, dependency review, repository cleanup. | Required checks pass, documentation matches behavior, clean checkout reproduces results, limitations documented. |

Each implementation work item starts on a new branch. Review the diff, run appropriate checks, stage an explicit file list, and supply a suggested commit message without committing. Exclude local settings, secrets, machine-specific output, downloaded tools, and unrelated changes.

Completion requires every R1-R8 process acceptance test, negative/lifecycle tests, formatting/static analysis, applicable sanitizer jobs, and generated documentation. Never describe planned workflows as executed or source review as runtime verification.

## 8. Scope and review points

Working assumptions are Windows x64, local same-user IPC, process-lifetime storage, session ownership, server-startup registration, C++20, JSON, and bounded multi-client operation. These are architectural scope decisions, not additional functional requirements. Revisit them if a concrete use case requires persistence, shared objects, client-defined executable classes, another platform, or legacy interoperability; update tests and the security model before extending scope.

The main risks are cancellation lifetime errors, ambiguous custom-class semantics, and unnecessary framework complexity. Bound domain methods, prove extensibility with a second type, and retain the smallest architecture that passes acceptance tests.

## 9. Primary technical references

Verify toolchain-specific details again during implementation:

- [Microsoft: synchronous and overlapped pipe I/O](https://learn.microsoft.com/en-us/windows/win32/ipc/synchronous-and-overlapped-input-and-output) explains completion and overlapped state.
- [Microsoft: named-pipe security/access rights](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights) describes security descriptors and access checks. The explicit DACL above is a project policy based on that model.
- [Microsoft: named-pipe instances](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-instances) describes serving multiple client connections.
- [LLVM: AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html) and [Microsoft: AddressSanitizer](https://learn.microsoft.com/en-us/cpp/sanitizers/asan?view=msvc-170) document instrumentation and Windows limitations.
- [LLVM: ThreadSanitizer supported platforms](https://clang.llvm.org/docs/ThreadSanitizer.html#supported-platforms) informs the separate Linux concurrency checks.
- [GitHub: GITHUB_TOKEN](https://docs.github.com/en/actions/concepts/security/github_token) and [workflow permissions](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#permissions) describe repository-scoped credentials and job permissions.
