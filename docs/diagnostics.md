# Logging and profiling

PipeForge links a common `pipeforge-diagnostics` static library into both executables. It adapts upstream [spdlog](https://github.com/gabime/spdlog) with its bundled fmt backend. There is no project logging framework or replacement serializer: [nlohmann/json](https://github.com/nlohmann/json) writes trace event objects, including escaping and Unicode handling.

## Command-line use

```powershell
./build/windows-release/pipeforge-client.exe --log-file build/diagnostics/client.log --trace-file build/diagnostics/client.trace.json --version
./build/windows-release/pipeforge-server.exe --log-file build/diagnostics/server.log --trace-file build/diagnostics/server.trace.json --help
```

Both outputs are opt-in. With no diagnostic paths, the normal help/version invocation creates no files, allocates no diagnostic backend, and performs no profiling clock reads. Logs and traces use distinct paths; aliases are rejected before either file is opened. Each process/session must own its own output paths.

Ordinary logs append with timestamp, application, severity, process ID, and thread ID. The sink rotates at 5 MiB, retaining three rotated files plus the active file. The API supports debug, info, warning, and error levels. User messages are values passed through a fixed format string. The CLI records startup/completion rather than argument payloads or IPC data.

Traces replace the explicitly selected file. `--trace-file` is rejected if profiling was compiled out. Unknown/duplicate/missing options return status 2 without initializing diagnostic files. File setup or ongoing write/flush failures return status 1; successful commands return 0. Recoverable spdlog setup exceptions are translated at the diagnostic I/O boundary. Ongoing sink errors set a session error flag and are reported on `finish()`; no recursive logging or background worker is used.

## Scope macros

```cpp
#include "pipeforge/diagnostics.hpp"

void process_request(pipeforge::diagnostic_session& diagnostics)
{
    PIPEFORGE_PROFILE_FUNCTION(diagnostics);

    {
        PIPEFORGE_PROFILE_SCOPE(diagnostics, "decode.request");
        // Decode the request here.
    }
}
```

`PIPEFORGE_PROFILE_FUNCTION` uses the standard `__func__` label. `PIPEFORGE_PROFILE_SCOPE` creates a nonmovable RAII scope covering the rest of its lexical block; use an inner block for a smaller region. Place one macro on each source line so its `__LINE__`-based local name is unique. Scope labels should describe operations, not contain user payloads.

The writer emits Chrome Trace complete (`X`) events with microsecond timestamp/duration, actual process ID, and session-local thread tracks. It uses a monotonic clock with a per-session origin. Nested events, concurrent threads, and exception unwinding are supported. Completed durations are measured before the event is serialized; synchronous logging and nested trace writes still add instrumentation overhead. These traces describe selected scopes, not sampled CPU stacks or allocation profiles.

The event budget defaults to 100,000 completed/admitted scopes, with at most 128 thread tracks and labels limited to 256 bytes. Additional events are dropped and counted in `otherData.droppedEvents`; invalid/truncated UTF-8 is replaced during serialization. A caller can set a smaller `trace_event_limit` when constructing a session. There is no unbounded event queue.

Call `finish()` after scopes end and check its result. If called while scopes are active, it rejects new scopes and returns false; the final outstanding scope writes the trailer and closes the sinks. Shared session state permits a scope to finish after the session owner requests shutdown. Construct/destroy an individual scope on the same thread. Destructors do not propagate sink I/O errors; unrecoverable allocation exhaustion remains fatal. Normal completion produces a valid JSON document; abrupt process termination can leave an incomplete trace.

## Viewing traces

Open [Perfetto UI](https://ui.perfetto.dev), choose **Open trace file**, and select the generated `.trace.json`. Compatible Chrome installations can also load it through `chrome://tracing`. The [Perfetto format documentation](https://perfetto.dev/docs/getting-started/other-formats#chrome-json-format) describes Chrome JSON and complete events.

This JSON is a diagnostic file format. The planned IPC contract remains binary Protobuf; no JSON fallback is added to named-pipe messages. Each file has its own monotonic origin; cross-process trace merging requires explicit clock alignment and is not implemented here.

M3 dispatch can borrow an explicit diagnostic session. It logs only request IDs, local operation names and result codes; class/member names and values are omitted. Fixed `server.dispatch` scopes are covered with profiling on and off. See [domain/services](domain-services.md).

## Disabling instrumentation

```powershell
cmake --preset windows-release "-DPIPEFORGE_ENABLE_PROFILING=OFF"
cmake --build --preset windows-release
```

With this option off, the macros generate no instrumentation and do not evaluate their arguments. Ordinary logs continue working. `PIPEFORGE_DISABLE_PROFILING` can also disable macros for an individual translation unit; tests exercise this variant. The compiler compatibility workflow validates the global profiling-off build, including diagnostic CLI handling.

## Validation and distribution

Tests cover real text output, append/flush/idempotent finish, alias protection, JSON escaping, nested/unwound scopes, thread isolation, bounded event counts, empty traces, deferred shutdown, disabled argument evaluation, CLI errors, and real file I/O failures. On Linux, the CLI integration test also checks a failed trace flush using `/dev/full`.

Each installed runtime ZIP includes `THIRD-PARTY-NOTICES.txt` with the spdlog, bundled fmt, and nlohmann/json license texts. These libraries are statically linked; headers, development libraries, and tooling are excluded. Isolated package tests generate and parse logs/traces from the extracted executable(s), in addition to checking startup/version and exact package contents.
