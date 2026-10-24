# PipeForge

[![Client build](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/build-client.yml/badge.svg?branch=master&event=push)](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/build-client.yml?query=branch%3Amaster+event%3Apush)
[![Server build](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/build-server.yml/badge.svg?branch=master&event=push)](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/build-server.yml?query=branch%3Amaster+event%3Apush)
[![Formatting](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/formatting.yml/badge.svg?branch=master&event=push)](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/formatting.yml?query=branch%3Amaster+event%3Apush)
[![Unit tests](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/unit-tests.yml/badge.svg?branch=master&event=push)](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/unit-tests.yml?query=branch%3Amaster+event%3Apush)
[![Integration tests](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/integration-tests.yml/badge.svg?branch=master&event=push)](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/integration-tests.yml?query=branch%3Amaster+event%3Apush)
[![End-to-end tests](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/end-to-end.yml/badge.svg?branch=master&event=push)](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/end-to-end.yml?query=branch%3Amaster+event%3Apush)
[![Static analysis and sanitizers](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/static-analysis.yml/badge.svg?branch=master&event=push)](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/static-analysis.yml?query=branch%3Amaster+event%3Apush)
[![API documentation](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/documentation.yml/badge.svg?branch=master&event=push)](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/documentation.yml?query=branch%3Amaster+event%3Apush)
[![Dependency review](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/dependencies.yml/badge.svg?branch=master&event=push)](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/dependencies.yml?query=branch%3Amaster+event%3Apush)

PipeForge is a C++20 client/server project for typed interprocess communication and
server-side objects over local Windows named pipes. It provides blocking and
future-based APIs, Protobuf contracts, session-owned storage and opt-in logging
and profiling.

## Build and run

Install CMake 3.25+, Ninja, Git, Python 3.10+ and MSVC Build Tools with the x64 C++
workload and Windows SDK. The Visual Studio IDE is optional.

```powershell
. ./scripts/Enter-Toolchain.ps1
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Start the server in one terminal:

```powershell
./build/windows-debug/pipeforge-server.exe --serve
```

After it prints `READY`, run a client in another terminal:

```powershell
./build/windows-debug/pipeforge-client.exe --demo-mixed
```

`--demo-sync` and `--demo-async` exercise the other call styles against the same
server. Each mode prints `R1-R8 scenario passed` on success. Type `stop` in the
server terminal for graceful shutdown. Use `--help` for endpoint, deadline,
concurrency and diagnostic options. See the [client guide](docs/client.md) for
library usage and cancellation semantics.

The first configure downloads pinned sources and builds the matching `protoc`.
Use `windows-release` for optimized binaries or `windows-clang` for clang-cl.
The `linux-debug` preset builds and tests portable libraries; the IPC runtime
requires Windows x64. [Build instructions](docs/build.md) cover offline sources,
profiling-disabled builds, sanitizers, fuzzing and documentation generation.

## Requirements and evidence

Named pipes are the required IPC transport. No socket fallback is provided.

| ID | Requirement | Automated evidence |
| --- | --- | --- |
| R1 | The client connects to the server through a named pipe. | Separate-process `ipc-process-acceptance`; [native stream tests](docs/testing.md). |
| R2 | The client makes both synchronous and asynchronous calls. | `ipc-process-acceptance`; [pending-future and blocking/async parity tests](docs/testing.md). |
| R3 | The client sends trivial data, including strings and numbers. | `ipc-process-acceptance`; [typed values, numeric boundaries and malformed-input tests](docs/testing.md). |
| R4 | The client creates server objects, retrieves state/attributes and invokes methods. | `ipc-process-acceptance`; [Counter behavior and argument tests](docs/testing.md). |
| R5 | One server receives synchronous/asynchronous connection requests and mixed calls. | `ipc-process-acceptance`, including simultaneous clients; [connection future tests](docs/testing.md). |
| R6 | The server retains client-provided data in an appropriate data structure. | `ipc-process-acceptance`; [value retention, pagination and quota tests](docs/testing.md). |
| R7 | Server code registers custom classes with functions and attributes discoverable by clients. | `ipc-process-acceptance`; [second-class registration and duplicate metadata tests](docs/testing.md). |
| R8 | The server stores the custom objects created by clients. | `ipc-process-acceptance`; [ownership/destruction tests](docs/testing.md) and [isolation tests](docs/testing.md). |

The [test guide](docs/testing.md) maps each requirement to exact CTest names,
describes the evidence and its limits, and explains how to reproduce it.

## Architecture and scope

- Separate executable composition and static libraries for contracts, codecs,
  transport, client scheduling, domain behavior and server services.
- Google's Protobuf implementation with explicit length framing over byte pipes;
  generated types stay inside the codec boundary.
- One overlapped transport, with blocking calls waiting on the same futures as
  asynchronous calls. Pending work, parser input and retained state have limits.
- Trusted server factories register Counter and TextBuffer metadata at startup.
  Execution remains server-side; clients receive IDs and typed snapshots.
- RAII owns native resources. Expected failures use typed results; throwing file
  I/O is translated at its adapter boundary.

Client and server run locally in the server token's logon context without elevation.
The DACL permits that logon SID and rejects remote clients. Same-user malicious
processes are outside a strong authentication boundary. Values and objects belong
to the creating session and disappear on disconnect; storage is not durable.
A sent mutation may have executed before a timeout, so requests are never replayed
automatically. Runtime code upload, legacy wire compatibility, a GUI and Windows
service integration are outside scope.

See [architecture](docs/architecture.md), [protocol](docs/protocol.md),
[domain services](docs/domain-services.md) and [Windows transport](docs/windows-transport.md).

## Logging and profiling

```powershell
./build/windows-debug/pipeforge-client.exe --demo-mixed --log-file build/client.log --trace-file build/client.trace.json
```

Both outputs are opt-in. Shared spdlog/fmt sinks write logs and bounded Chrome Trace
files; open traces in Perfetto or Chrome's trace viewer. Scope macros record RAII
durations and can be compiled out. Payloads and remote error text are not logged.
See [diagnostics](docs/diagnostics.md).

## Quality and dependencies

CI validates independent client/server builds, unit/integration/process tests,
extracted packages, clang-format, clang-tidy, Windows ASan, Linux ASan/UBSan/leak
detection, separate Linux TSan, decoder fuzzing and Doxygen. Badges show only
`master` push results; their links use the current repository URL and need updating
if it is renamed. A configured workflow is not evidence of a successful run.

Runtime sources are pinned and built statically: Protobuf/protoc, compatible
Abseil and bundled utf8_range, spdlog with bundled fmt, and nlohmann/json for trace
serialization. GoogleTest is test-only. The [dependency inventory](docs/third-party.md)
records revisions, hashes, licenses and advisory-review limits. Upstream downloads,
generated sources, binaries and generated HTML remain outside tracked source.

[The coding standards](docs/coding-standards.md) describe the C++ house style,
Core Guidelines checks and narrow reviewed exceptions. [API documentation](docs/api.md)
explains the Doxygen target and CI HTML artifact.

## Releases

Push a tag such as `V1.0`, `V1.0.0` or `V1.0.1` on a commit in `master` history to
publish that exact version. Lowercase `v` is also accepted. The pipeline validates
provenance, compiles and tests Windows x64 Release, then publishes client, server
and combined ZIPs containing runtime executables and third-party notices. Branch
pushes and pull requests do not publish. See [release instructions](docs/releases.md)
for commands, local packaging checks and failure handling.
