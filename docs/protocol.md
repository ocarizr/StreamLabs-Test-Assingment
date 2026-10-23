# Version-1 protocol

M2 supplies schemas, value contracts, codecs and stream framing. It performs no pipe I/O and does not implement storage, dispatch or object execution. M3 now implements those in-process services; pipe I/O and process-level client behavior remain M4/M5. The authoritative schema is [protocol.proto](../proto/pipeforge/v1/protocol.proto); the public C++ boundary is [protocol.hpp](../include/pipeforge/protocol.hpp).

## Build boundary

`pipeforge-contracts` owns the shared C++ contracts and semantic validation; domain code links it without depending on the codec or Protobuf. `pipeforge-protocol` is a static library containing maintained adapters and generated message objects, with a public dependency on contracts. Both executables link it through `pipeforge-cli` and report its supported revision in `--help`. Generated types and Google headers are private to the codec and tests. Domain/client interfaces exchange owned C++ values, never Protobuf ownership, pointers, handles, or an STL memory image.

The build pins Protobuf/protoc 36.2 (C++ runtime 7.36.2), Abseil 20250512.1 and Protobuf's bundled utf8_range. `protoc` is built from the same source tree as the full static runtime. An explicit custom command depends on that executable and the schema, so parallel builds generate outputs before compilation. Outputs live in `build/<configuration>/generated/wire/`; none are committed. System protoc/package fallback, zlib, gRPC and compression are disabled. Cross-compilation needs separate host-tool support and is not provided.

## Framing and use

Each frame is four unsigned little-endian length bytes, followed by exactly that many payload bytes. Length must be 1 through 1,048,576; the four header bytes are additional. Request and response payloads use separate envelope types, selected by the receiving endpoint's role. A direction is not inferred from the bytes. `encode_request`/`encode_response` return payloads; `frame` adds framing.

`frame_decoder::consume` accepts a byte span and returns consumed byte count plus at most one completed owned payload. It handles partial headers/payloads and stops at the first frame boundary. Feed the unconsumed suffix again to read concatenated frames. It never copies a whole arbitrary input chunk before checking its declared length, and retains at most one bounded payload. At EOF, `finish` reports truncated headers/payloads and accepts clean boundaries. Invalid lengths poison the decoder; close that session and create another decoder for a new connection. A decoder has one owner and is not thread-safe. Short writes, reads, deadlines and cancellation remain transport responsibilities.

```cpp
const pipeforge::protocol::request message{
    1, pipeforge::protocol::get_object{7}};
auto encoded = pipeforge::protocol::encode_request(message);
if (const auto* payload = std::get_if<pipeforge::protocol::bytes>(&encoded)) {
    auto packet = pipeforge::protocol::frame(*payload);
    // M4 will write the resulting bytes with partial-transfer handling.
}
```

Payload decode uses Google's `ArrayInputStream`/`CodedInputStream` with a total-byte limit and recursion limit 16, requires complete consumption, then checks semantics and aggregate limits. Parse/serialize status failures return typed `failure` values; ordinary validation has no try/catch. Allocation exhaustion remains an exception/fatal process resource failure, not a recoverable protocol result. Public encoder inputs are trusted in-process objects: they are copied into generated messages before validation; callers must not use the encoder to bound an already unbounded allocation.

## Envelopes and values

Both envelopes require explicit version presence with value 1 and a nonzero uint64 correlation ID. IDs are unique among outstanding requests within a session; the future session/client layer enforces uniqueness and response-operation matching. The codec alone has no pending-request table or session authorization state.

`Value` has exactly one alternative: UTF-8 text, signed int64 (`sint64`, ZigZag) or finite double. Empty text, zero integers, zero doubles and negative zero retain their selected type. NaN and infinities are invalid. NUL is permitted in text, but names use `[A-Za-z_][A-Za-z0-9_]*` and at most 128 bytes. Identifiers and stored IDs are data, not filesystem paths. Object IDs and value IDs are nonzero session-local handles; checking ownership belongs to M3/M4.

| Request | Success outcome | Contract for later dispatch |
| --- | --- | --- |
| `list_classes` | `classes` | Sorted, unique registered names, exclusive name cursor. |
| `describe_class` | `description` | Constructor parameters, attributes, methods, parameter/result kinds. |
| `put_value` | `value_record` | Allocate and retain a typed value, returning its ID and value. |
| `get_value` | `value_record` | Retrieve a session-owned value. |
| `list_values` | `values` | Increasing IDs, exclusive numeric cursor. |
| `create_object` | `object` | Invoke a trusted registered factory using uniquely named arguments. |
| `get_object` | `object` | Identity, registered class name and named typed state. |
| `release_object` | `released` | Empty acknowledgement; stale IDs fail. |
| `get_attribute` | `value` | Read declared state through the registered adapter. |
| `invoke_method` | `value` | Invoke server-side behavior with uniquely named arguments. |

Any operation may instead produce `error`. No source code, library path, executable payload, protobuf `Any`, JSON object, function pointer or recursive arbitrary value is accepted. Class names and argument/member names must be valid identifiers; duplicate names in argument/state/metadata collections are rejected. Attribute and method namespaces are separate. Value-kind enums must be one of text/integer/real, and error enums must be a declared nonzero code.

Pages default to 32 entries in the C++ contract; the wire requires an explicit nonzero limit up to 64. Start cursors are zero/empty. Success pages are sorted and unique; a nonfinal `next` cursor equals the final returned ID/name, while zero/empty marks the last page. Empty pages must be final. Storage quota enforcement and deciding whether another page exists are M3 work.

## Errors, compatibility and limits

Stable remote error codes are: 1 invalid request, 2 not found, 3 quota exceeded, 4 timeout, 5 cancelled, 6 access denied, 7 transport failure, 8 internal failure, 9 unsupported version, 10 numeric overflow. Messages are bounded UTF-8 text and must be safe server-supplied descriptions. Do not echo raw request data or exception details. M3's Counter arithmetic must check int64 bounds before mutation, return numeric-overflow and retain previous state. The wire encodes integer values with upstream Protobuf integer semantics; this is not a custom overflow/duplicate-field parser.

Local codec failures distinguish malformed data, invalid fields, unsupported version, resource limits and truncated framing. Framing/parse failures and zero correlation IDs require closing the receiving stream, with no trusted ID. After a complete parse, valid nonzero IDs permit a structured semantic error; `failure.request_id` and `close_session` describe that decision. Outgoing validation failures are reported before writing any frame. Unexpected response correlation or success-outcome mismatch must fail the session at the client layer in M5.

| Bound | Limit |
| --- | --- |
| Serialized payload | 1 MiB |
| Recursion during parsing | 16 |
| Text or one unknown length-delimited field | 64 KiB |
| Identifier | 128 ASCII bytes |
| Entries in each repeated field | 64 |
| Decoded known message nodes | 4,096 total |
| Unknown fields/groups | 128 total, recursion bounded |

The byte/recursion bounds apply during parsing; collection/node/unknown-field bounds are checked afterward before conversion/dispatch. A serialized-byte bound is not an exact heap bound: Protobuf may expand many small fields before semantic rejection. Tests include a valid-sized payload with 10,000 tiny entries. Future sessions must bound simultaneous decodes and separately account retained storage. Do not claim this codec enforces the 8 MiB session storage budget.

Unknown fields are accepted within these limits and discarded at conversion; they are never echoed. Unset or unknown operation/outcome alternatives are invalid. Protobuf's standard semantics apply: duplicate scalar fields use the last value, repeated embedded messages can merge, and a later distinct oneof alternative replaces the earlier alternative. A custom scanner is deliberately avoided. Never reuse field numbers/names after removal; reserve them, and add fields compatibly. A field addition may be wire-compatible without being understood by an older application. Unsupported major versions are rejected explicitly.

## Tests, fuzzing and measurements

Unit tests exercise every request and response shape, type presence, int64 limits, finite doubles, UTF-8, invalid enums/names, duplicate entries, known/unknown resource limits, deep groups, malformed/trailing bytes, golden bytes, merge/oneof semantics and framing boundaries. A shared fuzz harness checks successful decode/encode/decode invariants and fragmented frame progress. A deterministic 512-mutation regression runs through CTest on Windows/Linux.

`protocol-toolchain` checks the exact protoc version, accepts matching generated headers, and intentionally changes the runtime-version macro in an isolated translation unit to prove that Google's generated guard rejects incompatibility. It does not modify dependency or generated files.

The Linux Clang libFuzzer workflow applies ASan/UBSan consistently to all compiled targets, including libprotoc/protoc and Abseil, and adds fuzz coverage to the maintained codec/generated messages and full Protobuf runtime. It uses leak detection, runs a 30-second PR/master smoke and a 600-second weekly run, and retains logs/crash inputs. Corpus files are reviewed hexadecimal fixtures, materialized into payload and framed seeds by `scripts/prepare_fuzz_corpus.py`. Local deterministic fuzz smoke is distinct from executing this Linux job.

See [M2 verification and baseline](m2-verification.md) for executed results and their limits. Profiling/logging remain opt-in: protocol functions do no global logging and do not log peer payloads. M3 instruments the service boundary with fixed scope labels and safe IDs/categories; transport/client call sites remain M4/M5 work. No timing thresholds are CI gates. Binary encoding is not compression, and named pipes are local IPC, not a network benchmark.

Primary references: [Protobuf encoding](https://protobuf.dev/programming-guides/encoding/), [C++ runtime compatibility](https://protobuf.dev/support/cross-version-runtime-guarantee/#cpp), [CodedInputStream](https://protobuf.dev/reference/cpp/api-docs/google.protobuf.io.coded_stream/), and the [pinned dependency build](https://github.com/protocolbuffers/protobuf/blob/v36.2/CMakeLists.txt).
