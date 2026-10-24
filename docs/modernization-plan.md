# PipeForge modernization plan

Status: M0-M5 implemented; M6 planned. Updated: 2026-10-23.

M1 establishes CMake, separate executables, tests/CI/master badges, C++ formatting and control-block spacing, static/runtime safety checks, spdlog logging, bounded Chrome Trace scope profiling, and tag-only release packaging. R1-R8 refer to the numbered functional requirements in the README. See [build instructions](build.md), [coding standards](coding-standards.md), [diagnostics](diagnostics.md), [release policy](releases.md), and [M1 verification](m1-verification.md).

M2 implements the [version-1 protocol](protocol.md): schemas, independent C++ contracts, full static Protobuf runtime/compiler integration, bounded validation, incremental framing, negative/golden tests, fuzz harness and initial size/CPU measurements. Domain services are implemented in M3; Windows transport and client runtime are implemented in M4/M5. Protocol code has no global logging or payload logging. Executed checks and remote-validation limits are recorded in [M2 verification](m2-verification.md).

M3 implements [domain/services](domain-services.md): shared contracts/validation without a codec dependency, a bounded startup registry, factories, session-owned value/object stores, Counter and TextBuffer, checked arithmetic, clone/validate/commit invocation, and a correlated dispatcher with safe optional diagnostics. In-process tests cover R3/R4/R6/R7/R8; M4/M5 add native transport and cross-process acceptance. See [M3 verification](m3-verification.md).

M4 implements the [Windows transport](windows-transport.md), and M5 composes it with
[client futures and executable demonstrations](client.md). Three bounded client
workers separate writes, reads and deadlines; sixteen calls may be queued/in flight.
Actual byte-pipe backpressure, cancellation, peer failures, process scenarios and
installed ZIPs are covered. See [M4 verification](m4-verification.md) and
[M5 verification](m5-verification.md). M6 retains the final documentation/CI audit,
including Linux TSan coverage of the now-implemented portable client scheduler.

## 1. Source-review findings

These are historical observations from the M0 source review, not results of executing the legacy binaries. M1 retired the solution/project files and untracked generated artifacts. The unused `Client/` and `Server/` sources were removed during the pre-M3 cleanup; the historical paths below refer to Git history.

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
| JSON as the initial proposed format | Official Google Protobuf C++ implementation and shared static protocol library | Typed schemas and compact binary encoding; introduces code generation and a runtime dependency. Measure performance rather than assuming improvement. |
| Catch-all helpers | Native result checking and targeted exception translation | Preserve failure causes; do not treat native status codes as thrown exceptions. |

### Components and dependencies

Target layout, introduced incrementally through M5:

```text
CMakeLists.txt / CMakePresets.json
include/pipeforge/          Public value contracts and client API
proto/pipeforge/v1/         Versioned Protocol Buffers schemas
src/protocol/              Serialization, framing, validation
src/contracts/             Shared semantic validation, no codec dependency
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

The CMake project is `PipeForge`, with public namespace `pipeforge` and executable targets `pipeforge-client` and `pipeforge-server`.

Domain code has no transport/client/Win32 dependencies. Protocol code depends on value contracts; transport moves bytes without interpreting business operations. Server services depend on narrow repository contracts and the class registry. Entry points compose implementations. Client links contracts and transport, not server implementation.

Build `pipeforge-protocol` as a STATIC CMake target containing generated `.pb.cc` sources, framing, validation, and domain conversion adapters. Both executables link it, directly or through their application libraries. Each executable owns its linked copy; this is code reuse, not shared process memory. Keep generated Protobuf types internal to the protocol boundary and out of domain/public client interfaces. The protocol target links the upstream `protobuf::libprotobuf` target so CMake propagates required link dependencies. Use the full runtime initially; evaluate the lite runtime only if measurements justify its reflection/diagnostics tradeoffs.

Apply SOLID through separate responsibilities, extensibility by registration, consistent lifetime/error contracts, small interfaces, and injected I/O dependencies. Apply DRY to codecs, validation, dispatch, and error translation. Avoid an interface for every class or abstractions for hypothetical backends.

Use Factory/Registry for classes, Repository for stored state, Adapter for Win32 I/O, and RAII for ownership. Prefer a dispatcher table over a hierarchy for every command. Each pattern must solve a concrete coupling or lifecycle problem.

### Wire contract

- One local duplex endpoint, initially `\\.\pipe\pipeforge-v1`. Tests use unique names and explicit readiness signals.
- Byte-mode pipe with a 4-byte unsigned little-endian payload length followed by one binary Protobuf envelope. Protobuf does not supply top-level message boundaries for a stream; retain PipeForge framing and partial-transfer handling.
- Define proto3 schemas in package `pipeforge.wire.v1`. Request envelopes carry explicit version presence, a nonzero request ID, and a `oneof` of typed operation messages. Responses echo the ID and contain a `oneof` result/error. Validate required application fields after parsing; a successful parse does not establish a valid request.
- Reuse Google's `protoc` and C++ runtime for encoding/decoding. Implement PipeForge schemas, adapters, and validation, not a new serializer or byte packer. Do not introduce JSON payloads inside Protobuf or gRPC services/stubs.
- Define a typed `Value` with `oneof` string, `sint64`, and double alternatives. ZigZag encoding supports compact small negative integers; integers no longer need decimal-string conversion. Enforce finite doubles, UTF-8 validity, string lengths, and explicit presence, including zero and empty-string values. Model object snapshots, attributes, and method arguments with bounded typed entries, not arbitrary executable types or `Any` payloads.
- Never reuse field numbers; reserve removed names/numbers and avoid incompatible type changes. Permit bounded unknown fields for additive evolution, but reject unsupported versions, unset/unknown operation alternatives, and invalid enums. Follow Protobuf semantics: duplicate singular scalar fields use the last value, embedded messages may merge, and a later different `oneof` alternative replaces the selected alternative. Do not require a custom duplicate-field scanner. Reject duplicate application-level attribute/argument names where ambiguity matters.
- Validate lengths before allocation; handle partial transfers and concatenated frames. Never assume one read equals one request or transmit an STL representation.
- Initial defaults: 1 MiB frame payload, nesting depth 16, 64 KiB strings, 4 sessions, one executing request per server session (M4) and up to 16 pending client calls (M5), 100 stored values and 100 objects per session, 8 MiB accounted session state. Registry types declare bounded state/results. Reject mutations before quotas are exceeded; paginate list responses to stay within frame limits.
- Connection/request deadlines default to 5 seconds, configurable within documented bounds. Bound queues/workers; overload returns a stable error.
- Operations: describe/list classes, put/get/list values, create/get/release object, get attribute, invoke method. Specify schemas, numeric-overflow behavior, and error codes before implementation.

Use bounded `CodedInputStream` decoding with total-byte and recursion limits, require complete frame consumption, and validate collection cardinalities and application constraints before dispatch. A bounded wire frame is not an exact heap-allocation bound: decoded objects and unknown fields can expand in memory. Avoid recursive value schemas, limit simultaneous decodes, account stored state separately, and test adversarial field counts under sanitizers. Unknown fields must remain within the same resource limits and must not be echoed as arbitrary payloads.

Binary encoding is not general-purpose compression. Start without a compression layer. Measure serialized bytes, encode/decode CPU time and allocations, and end-to-end pipe latency/throughput for small control requests, object operations, long strings, and numeric batches. Record build configuration and payloads; use any test-only JSON reference solely for comparison, not as a second supported wire format. Do not make noisy performance thresholds mandatory PR gates before establishing a repeatable baseline.

### Concurrency, lifetime, and errors

The server accepts overlapped connections and maintains one read and one serialized write stream per session. One bounded dispatcher serializes domain operations, avoiding concurrent object mutation initially. Registered methods must be bounded and perform no blocking I/O. This is a small concurrency model, not a throughput claim.

The client owns a bounded I/O worker and correlates promises by request ID. `connect_async` and `call_async` return futures; blocking APIs wait on those same operations. Every pending promise resolves exactly once, including timeout, cancellation, disconnection, and shutdown.

Each operation owns its buffer, initialized `OVERLAPPED`, and event until completion is observed, including after `CancelIoEx`. Requesting cancellation does not authorize destroying operation state. Destructors release resources without throwing.

Objects and values belong to their creating session, survive until explicit release/disconnect, and are inaccessible from other sessions. IDs are opaque; possession alone does not authorize cross-session access. Reconnection starts a new session. Do not transparently replay mutations: a timeout can leave execution indeterminate and does not guarantee rollback.

Expected failures use a typed result, implemented with `std::variant` in C++20. Categories include invalid request, not found, quota exceeded, timeout, cancelled, access denied, transport failure, and internal failure. Capture native codes immediately after failed calls. Wire errors expose stable codes; sensitive diagnostics stay local.

Check Win32 return values directly. Protobuf parse/serialize status failures become typed protocol errors; do not add try/catch around status-returning APIs. Catch specific exceptions only from adapters that actually throw, such as optional filesystem/stream I/O, at a boundary where recovery or translation is meaningful. Keep ordinary validation exception-free. Avoid blanket catch-and-continue helpers and pervasive try/catch blocks.

## 4. Security and IPC failure model

Treat local peers as untrusted input. The baseline is a same-user, non-elevated desktop server, not a privileged service or public network service.

- Use an explicit DACL restricted to the intended user/logon context; reject remote clients using `PIPE_REJECT_REMOTE_CLIENTS`. Test other-user denial where runner permissions allow, and document manual checks otherwise.
- Use `FILE_FLAG_FIRST_PIPE_INSTANCE` for the first listener instance to fail on an occupied name. Later instances belong to that listener. Test this lifecycle. Same-user malicious processes remain outside a strong authentication boundary; do not claim protection against a compromised account.
- Handle immediate/pending completion, `ERROR_IO_PENDING`, `ERROR_PIPE_CONNECTED`, busy/unavailable pipe, zero-byte EOF, peer exit, truncated frames, failed writes, cancellation races, and shutdown. Avoid busy spins, unconditional flushes, and unbounded retries.
- Reject malformed/truncated Protobuf, invalid UTF-8, unsupported versions, duplicate in-flight request IDs, unknown response IDs, invalid fields/members/numbers, oversized lengths, excessive nesting/cardinality, and quota exhaustion. Framing violations close the session; undecodable envelopes also close it because no request ID can be trusted. Decodable envelopes with invalid operations receive structured errors when correlation is valid. Bounded unknown fields follow the compatibility policy above.
- Accept no source uploads, library paths, filesystem paths, shell commands, function pointers, or executable code. Remove dynamic code loading.
- Do not log payloads by default. Log operation, correlation ID, error category, and native code where useful.

Static analysis does not prove memory or IPC safety. Combine ownership design, bounded parsing, failure injection, sanitizers, fuzzing, and real transport tests. Document residual limitations.

## 5. TDD and verification

Reuse upstream GoogleTest with CTest, pinned when introduced; GoogleMock is optional for behavioral interactions that benefit from it. Do not build a replacement testing framework. Unit tests inject fake byte transports and a controllable clock/completion scheduler. Integration tests use actual Windows named pipes. Assert observable behavior and cleanup rather than mirroring implementation details.

For every slice: write a failing behavioral test, verify its intended failure, implement the smallest passing behavior, then refactor with tests green. Record brief red/green evidence in the work summary; commits remain manual. An unbuildable legacy baseline is not evidence of a meaningful failing behavioral test.

| Layer | Required cases |
| --- | --- |
| Protocol | Independent known Protobuf vectors, semantic round trips, split headers/payloads, concatenated frames, overflow/oversize lengths, truncated varints/length-delimited fields, invalid wire types, recursion/cardinality limits, UTF-8 and numeric boundaries, zero/empty presence, unknown fields, unsupported operations/versions, duplicate-field semantics, and older/newer additive-schema fixtures. Do not assume Protobuf serialization is canonical or compare arbitrary encodings byte-for-byte. |
| Domain | Second registered class, metadata, duplicate registration, factory failure, stateful methods, wrong arguments, overflow without mutation, independent instances, stale IDs, ownership, quotas. |
| Application/client | Dispatch, typed errors, sync/async parity, out-of-order correlation, unknown response IDs, backpressure, deadlines/cancellation races, exactly-once promise completion. |
| Transport | Immediate/pending connect and I/O, connect race, short transfers, disconnect mid-frame, unavailable/busy pipe, timeout, cancellation followed by completion, pending-operation shutdown. |
| End to end | Separate processes satisfy R1-R8; mixed-style sessions; isolation; repeated connect/disconnect and resource-count checks for monotonic leaks. |

Use readiness/completion signals and test deadlines instead of timing-sensitive sleeps. Capture failure diagnostics and always reap subprocesses. Collect coverage to locate omissions; no percentage substitutes for the required behavioral cases.

## 6. Build, CI, and API documentation

Use target-scoped language standards, warnings, include paths and dependencies. Apply sanitizer compile/link options consistently before creating targets or dependencies to preserve Protobuf's ASan-dependent message layout. Provide configure/build/test presets and clean-checkout CLI instructions. Pin Protobuf, GoogleTest, and their transitive sources by revision/hash and document an offline cache path. Do not commit downloads or binaries. Export a real compilation database using Ninja/Clang for clang-tidy.

### Upstream dependency integration

M1 acquires GoogleTest at immutable revision `52eb8108c5bdec04579160ae17225d66034bd723` (v1.17.0), plus SHA256-verified spdlog v1.17.0 and nlohmann/json v3.12.0 archives for diagnostics. M2 adds SHA256-verified Protobuf 36.2 and compatible Abseil 20250512.1. Matching protoc/runtime and utf8_range are built from the pinned sources; optional downloads, zlib and system-package fallback are disabled. JSON serializes trace files and a benchmark-only reference, never IPC messages. See [dependency notices](third-party.md).

Use the official [protocolbuffers/protobuf](https://github.com/protocolbuffers/protobuf) and [google/googletest](https://github.com/google/googletest) implementations. Both provide CMake integration; PipeForge owns its schemas, adapters, and tests. No custom Protobuf parser, byte packer, or testing framework is planned.

- Centralize acquisition in `cmake/Dependencies.cmake` using CMake FetchContent with immutable revisions or verified archive hashes. Select supported releases during M1/M2, record their licenses/notices, and update them through reviewed changes. Do not fetch moving branches or resolve unspecified system packages silently.
- Pin the compatible Abseil and other transitive inputs required by the selected Protobuf release. Declare/reuse their upstream CMake targets before configuring Protobuf where necessary; audit fallback downloads and prevent unpinned configure-time network access. FetchContent alone is not a dependency lockfile.
- Build the official Protobuf runtime statically (`protobuf_BUILD_SHARED_LIBS=OFF`) and build `protoc` from the same pinned release. Disable upstream examples/tests in normal consumer builds. GoogleTest is test-only and links through `GTest::gtest_main` and CTest discovery. Align MSVC runtime selection, architecture, and Debug/Release settings across project and dependencies; a static library does not require a static C runtime.
- Compile `.proto` files through the selected upstream CMake generation helpers or explicit custom commands depending on the `protoc` target. Put generated `.pb.h`/`.pb.cc` files under the build tree and model generation dependencies explicitly for safe parallel builds. Commit schemas and build integration, not generated source or downloaded dependency trees.
- Protobuf C++ requires matching generated-code/runtime versions and offers no ABI stability across releases. Keep compiler, generated code, and runtime pinned together; do not mix an arbitrary installed `protoc` with another runtime. This build constraint is separate from schema-level wire compatibility between processes.
- Provide a documented prepopulated FetchContent source/cache path for offline builds and CI caches keyed by dependency revisions, compiler, architecture, runtime choice, and sanitizer configuration. Native Windows x64/Linux builds are the baseline; future cross-compilation requires a host-runnable `protoc` of the matching version.
- Apply project warnings-as-errors, formatting, clang-tidy rules, and Doxygen completeness checks to maintained project code, excluding upstream/generated implementation code. Continue instrumenting dependencies where compatible with sanitizer builds so decoding paths receive coverage. CI must verify clean schema generation, static linking, both consumers, and intentional version-mismatch failure.

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

M1 validation covers MSVC Debug/Release, clang-cl, portable Linux scaffold tests, explicit formatting, clang-tidy/analyzer activation probes, Linux ASan/UBSan with leak detection, and isolated release-package end-to-end tests. M2 extends those jobs to codecs, generated messages and Protobuf runtime instrumentation, plus compiler/runtime mismatch rejection, deterministic fuzz smoke and a bounded libFuzzer workflow. M3 adds domain unit tests and codec/service integration under the existing labels; shared validation remains covered by decoder fuzzing. Independent workflows retain master-only status filtering. M4 adds real Windows transport tests under integration and a whole-build clang-cl/ASan job. TSan and generated documentation remain planned. There is no dependency binary cache; cache keys above apply if caching is introduced.

### Release publishing (added to M1)

Only a pushed tag matching `Vmajor.minor` or `Vmajor.minor.patch` (lowercase `v` also accepted) may publish. Validate the exact tag/event/checkout commit and require ancestry in fetched `origin/master`, including historical master commits. Branch pushes, pull requests, and manual dispatch never publish.

Build the validated commit on Windows x64 in Release, inject its normalized version, run tests, and install client/server runtime components into fresh staging directories. Produce exactly three ZIP assets: client, server, and bundle. Exclude sources, development libraries, symbols, and tests; include only necessary runtime files. Run isolated extraction/startup/version checks before publishing. Use static CRT for M1 executables; extend manifests and notices explicitly if future runtime dependencies are added.

Separate read-only validation/build jobs from a write-authorized publication job. Recheck the remote tag, upload tested ZIPs to a draft release, then publish. Never silently overwrite existing releases or moved tags. The workflow is implemented in M1; remote execution awaits a user push, and this work does not publish a release. Detailed commands and failure handling are in [release instructions](releases.md).

Document public interfaces with Doxygen comments describing purpose, parameters, returns/errors, ownership, thread safety, cancellation, and side effects. Explain non-obvious protocol/security/lifetime choices near code. Avoid comments that merely restate statements. Keep code and documentation in English for consistency across the project.

Generated HTML artifacts are the initial deliverable. GitHub Pages is the preferred optional published API reference. A wiki summary is optional: confirm wiki availability and authentication, generate into a temporary checkout, and update generated pages only from trusted branch builds or manual dispatch. Do not assume `GITHUB_TOKEN` can write to the separate wiki repository. Publishing setup must not block required build/test validation.

## 7. Implementation milestones

| Milestone | Work and test-first focus | Exit criteria |
| --- | --- | --- |
| M0: planning (complete) | Audit, requirement mapping, replacement decisions, ignore rules, remove `README.docx`. | Reviewable plan and explicit scope. |
| M1: build and test harness (implemented) | CMake/pinned dependencies, executable scaffolds, CTest/CI/badges, explicit C++ style and control spacing, clang-tidy/Linux sanitizers, shared spdlog logging and scope profiling, tag-only release publishing. Retire IDE build entry points and tracked output. | Clean-source build/test; profiling-on/off and diagnostic lifecycle tests; static/format checks; isolated ZIP log/trace checks. Merged on master; no IPC parity claimed. |
| M2: protocol (implemented) | Version-1 schemas/contracts, all operation/outcome shapes, constructor metadata, bounded full-runtime Protobuf codec, incremental framing, malformed/golden/negative tests, compiler-version guard test, deterministic and libFuzzer harnesses, size/CPU baseline. | Local protocol/generation/consumer checks and deterministic fuzz smoke; static/format checks; Linux sanitizer/libFuzzer workflows require remote execution. See M2 verification for exact executed checks. |
| M3: domain/services (implemented) | Reuse M2's typed values, operations and metadata; bounded repositories, registry/factories, Counter and second type, object lifecycle, dispatcher; safe diagnostic IDs/categories at the service boundary. | In-process R3/R4/R6/R7/R8 tests, including ownership, quotas, duplicate registration, paging cursors and checked arithmetic without state mutation on overflow. Codec/service integration, lifecycle probes and safe logs/traces are covered; see M3 verification. |
| M4: Windows IPC (implemented) | RAII wrappers, DACL, overlapped accept/read/write, M2 incremental framing, deadlines, cancellation, bounded simultaneous decodes/sessions, shutdown. | Real Windows framing/security/peer-exit/lifecycle tests, bounded workers with serialized domain access, pending-operation cleanup under ASan; fatal parse/frame errors close while correlated semantic errors can continue. See M4 verification for executed checks and limitations. |
| M5: client and demonstration (implemented) | Future-based connect/call, blocking wrappers, CLI/process tests; response correlation and outcome matching; opt-in call-site profiling/logging using M1 diagnostics. | Separate processes exercise R1-R8 and simultaneous mixed sessions; controlled peers verify out-of-order replies, invalid IDs/outcomes, admission bounds and exactly-once completion. Extracted split/bundle ZIPs repeat the process scenarios. See M5 verification for executed checks and remaining platform work. |
| M6: quality/delivery | Complete CI matrix, usage/protocol/Doxygen guides, optional publishing, dependency review, repository cleanup. | Required checks pass, documentation matches behavior, clean checkout reproduces results, limitations documented. |

Each implementation work item starts on a new branch. Review the diff, run appropriate checks, stage an explicit file list, and supply a suggested commit message without committing. Exclude local settings, secrets, machine-specific output, downloaded tools, and unrelated changes.

Completion requires every R1-R8 process acceptance test, negative/lifecycle tests, formatting/static analysis, applicable sanitizer jobs, and generated documentation. Never describe planned workflows as executed or source review as runtime verification.

## 8. Scope and review points

Working assumptions are Windows x64, local same-user IPC, process-lifetime storage, session ownership, server-startup registration, C++20, official Protobuf binary serialization through a common static library, and bounded multi-client operation. These are architectural scope decisions, not additional functional requirements. Revisit them if a concrete use case requires persistence, shared objects, client-defined executable classes, another platform, or legacy interoperability; update tests and the security model before extending scope.

The main risks are cancellation lifetime errors, ambiguous custom-class semantics, and unnecessary framework complexity. Bound domain methods, prove extensibility with a second type, and retain the smallest architecture that passes acceptance tests.

## 9. Primary technical references

Verify toolchain-specific details again during implementation:

- [Protobuf CMake build documentation](https://github.com/protocolbuffers/protobuf/blob/main/cmake/README.md) and [upstream build options](https://github.com/protocolbuffers/protobuf/blob/main/CMakeLists.txt) inform runtime/compiler integration and transitive dependency handling. Lock exact revisions when implementing.
- [Protobuf encoding](https://protobuf.dev/programming-guides/encoding/) describes wire types, integer encoding, and duplicate-field semantics; [C++ version compatibility](https://protobuf.dev/support/cross-version-runtime-guarantee/#c-and-rust-specific-guarantees) requires matching generated code and runtime.
- [GoogleTest CMake quickstart](https://google.github.io/googletest/quickstart-cmake.html) documents FetchContent, test target linking, and CTest discovery.

- [Microsoft: synchronous and overlapped pipe I/O](https://learn.microsoft.com/en-us/windows/win32/ipc/synchronous-and-overlapped-input-and-output) explains completion and overlapped state.
- [Microsoft: named-pipe security/access rights](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights) describes security descriptors and access checks. The explicit DACL above is a project policy based on that model.
- [Microsoft: named-pipe instances](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-instances) describes serving multiple client connections.
- [LLVM: AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html) and [Microsoft: AddressSanitizer](https://learn.microsoft.com/en-us/cpp/sanitizers/asan?view=msvc-170) document instrumentation and Windows limitations.
- [LLVM: ThreadSanitizer supported platforms](https://clang.llvm.org/docs/ThreadSanitizer.html#supported-platforms) informs the separate Linux concurrency checks.
- [GitHub: GITHUB_TOKEN](https://docs.github.com/en/actions/concepts/security/github_token) and [workflow permissions](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#permissions) describe repository-scoped credentials and job permissions.
