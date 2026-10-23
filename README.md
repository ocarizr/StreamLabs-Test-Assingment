# PipeForge

[![Client build](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/build-client.yml/badge.svg?branch=master&event=push)](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/build-client.yml?query=branch%3Amaster+event%3Apush)
[![Server build](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/build-server.yml/badge.svg?branch=master&event=push)](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/build-server.yml?query=branch%3Amaster+event%3Apush)
[![Formatting](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/formatting.yml/badge.svg?branch=master&event=push)](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/formatting.yml?query=branch%3Amaster+event%3Apush)
[![Unit tests](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/unit-tests.yml/badge.svg?branch=master&event=push)](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/unit-tests.yml?query=branch%3Amaster+event%3Apush)
[![Integration tests](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/integration-tests.yml/badge.svg?branch=master&event=push)](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/integration-tests.yml?query=branch%3Amaster+event%3Apush)
[![End-to-end tests](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/end-to-end.yml/badge.svg?branch=master&event=push)](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/end-to-end.yml?query=branch%3Amaster+event%3Apush)
[![Static analysis and sanitizers](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/static-analysis.yml/badge.svg?branch=master&event=push)](https://github.com/ocarizr/StreamLabs-Test-Assingment/actions/workflows/static-analysis.yml?query=branch%3Amaster+event%3Apush)

PipeForge is a C++ client/server project for typed interprocess communication and server-side object interaction over Windows named pipes.

Badges report only `push` runs on `master`; they do not report pull-request or feature-branch results. Each links to its own validation workflow. They acquire a status after those workflows first run on master. URLs use the current Git remote and should be updated when the repository is renamed.

## Requirements

The project defines the following functional requirements:

NamedPipe is the only hard requirement for the IPC

1. The client should be able to connect to the server through a NamedPipe
2. The client should be able to make both sync and async calls to the server
3. The client should be able to send trivial data (strings, numbers) to the server
4. The client should be able to create objects on the server (based on req-7 below), retrieve them, their attributes and call methods on them
5. The server should be able to receive both sync/async connection requests from clients
6. The server should be able to store data provided by the client via NamedPipe in a reasonable data structure
7. The server should be able to register a custom class (w/ related functions, attributes) which can be used by the client (see req-4)
8. The server should be able to store the custom objects created by the client for the custom class created in req-7

## Project status

M1 supplies CMake, separate executable scaffolds, tests/CI, formatting/static analysis, logging/profiling and tag-only releases. M2 adds the version-1 Protobuf schemas, shared static protocol library, bounded codecs and incremental framing, adversarial tests, decoder fuzzing workflow and a size/CPU baseline. The executables support `--help` and `--version` with optional diagnostics; starting IPC remains unavailable until later milestones.

The active implementation lives in `apps/`, `include/`, `src/` and `proto/`. The unused legacy `Client/` and `Server/` sources, Visual Studio solution and project files have been removed; historical versions remain available in Git history. Generated IDE/build outputs are not tracked.

The [modernization plan](docs/modernization-plan.md) defines the remaining architecture and acceptance criteria. See the [protocol contract](docs/protocol.md), [M2 verification/baseline](docs/m2-verification.md), [build instructions](docs/build.md), [release instructions](docs/releases.md), and [dependency notices](docs/third-party.md). Domain behavior starts in M3 and Windows named-pipe transport in M4.

## Build and test

Install CMake 3.25+, Ninja, Python 3.10+, and MSVC Build Tools with the x64 C++ workload and Windows SDK. The Visual Studio IDE is optional. In PowerShell from the repository root:

```powershell
. ./scripts/Enter-Toolchain.ps1
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
./build/windows-debug/pipeforge-client.exe --help
./build/windows-debug/pipeforge-server.exe --version
```

The first configure acquires pinned runtime dependencies and builds the matching `protoc`; test builds also acquire GoogleTest. This takes longer than the M1 build. Use `windows-release` for optimized builds or `windows-clang` for clang-cl. The portable core can be built with `linux-debug`; this does not provide Linux named-pipe support. Formatting uses clang-format 19.1.5 for C++ and schemas.

The [coding standards](docs/coding-standards.md) define the project's C++20 layout and Core Guidelines policy, analyzer checks, and reviewed exceptions. clang-tidy combines Clang Static Analyzer, bug-prone, CERT, ownership/bounds, and other guideline diagnostics. It detects classes of defects rather than proving that all undefined behavior or leaks are absent.

Unit tests, process/Git integration tests, and isolated installed-package end-to-end tests have separate CTest labels and workflows. Integration/end-to-end badges currently cover the scaffold and distribution, not the named-pipe behavior planned for later milestones.

## Logging and profiling

```powershell
./build/windows-release/pipeforge-client.exe --log-file build/client.log --trace-file build/client.trace.json --version
```

Logging uses upstream spdlog/fmt; the common diagnostic library writes bounded Chrome Trace files through spdlog sinks. `PIPEFORGE_PROFILE_SCOPE` and `PIPEFORGE_PROFILE_FUNCTION` record RAII durations with thread/process information. Open traces in Perfetto or Chrome's trace viewer. Both file outputs are opt-in; profiling can be compiled out. See [diagnostic usage and lifecycle](docs/diagnostics.md).

## Design choices

The proposed design replaces the original decisions:

- Build separate client and server executables with a root CMake project, Ninja presets, C++20, and a Windows SDK toolchain without requiring the Visual Studio IDE.
- Separate the protocol, Windows pipe transport, client API, server services, and domain objects. Share serialized value contracts between processes.
- Use Google's existing C++ Protocol Buffers implementation, with `.proto` schemas and generated codecs in a `pipeforge-protocol` static library linked by both executables. Keep explicit length framing over named pipes; gRPC is unnecessary for this design.
- Use one request dispatcher and one overlapped transport implementation. Synchronous calls wait on the same operation used by the asynchronous API.
- Register trusted C++ classes on the server through factories and metadata. Object state and method execution remain on the server; clients receive IDs and value snapshots.
- Start with bounded, in-memory storage. A database and persistence across server restarts are not required by the project.
- Use RAII, typed errors for expected failures, and targeted exception handling at I/O adapters where an API actually throws.
- Develop behavior through TDD. Validate formatting, static analysis, memory checks, real pipe integration, and generated API documentation in CI.

The plan explains the architectural tradeoffs and how server-side class registration satisfies requirements 4, 7, and 8.

## Dependencies

- [spdlog](https://github.com/gabime/spdlog), v1.17.0 at revision `79524ddd08a4ec981b7fea76afd08ee05f83755d`: static runtime logging and profiling file sinks, with bundled fmt.
- [nlohmann/json](https://github.com/nlohmann/json), v3.12.0 at revision `65ee68451d8eb2b5f3a30b410476ab83deb3289b`: diagnostic trace serialization only.
- [GoogleTest](https://github.com/google/googletest), v1.17.0 at revision `52eb8108c5bdec04579160ae17225d66034bd723`: test-only, built through CMake FetchContent and integrated with CTest.
- [Protocol Buffers](https://github.com/protocolbuffers/protobuf), v36.2 (`424e1b0a184baa5a3c75061a1febd5f60a0a3707`): matching official `protoc` and full C++ runtime 7.36.2; bundled utf8_range validates UTF-8.
- [Abseil](https://github.com/abseil/abseil-cpp), 20250512.1 (`76bb24329e8bf5f39704eb10d21b9a80befa7c81`): pinned Protobuf transitive dependency.

Downloaded sources and generated output remain in the build directory. The plan defines version matching, transitive dependencies, and reproducibility checks. Binary serialization efficiency will be measured rather than assumed.

## Releases

Pushing a version tag such as `V1.0`, `V1.0.0`, or `V1.0.1` publishes a release only if the tagged commit belongs to `master` history. Lowercase `v` is also accepted. The pipeline builds that exact commit and produces three Windows x64 ZIPs: client, server, and bundle. Each contains executable runtime files and required third-party notices. Ordinary branch pushes and pull requests never publish. See [release instructions](docs/releases.md) for examples and failure handling.

## Assumptions

These are explicit scope decisions for the proposed implementation:

- Windows x64 is the supported runtime. Portable domain/protocol code can also be tested on Linux; CMake alone does not make Win32 IPC portable.
- Client and server run locally under the same Windows user without elevation. Remote clients and other users are denied by transport policy.
- Both call styles work against the same running server and endpoint. Concurrent sessions and resource use have explicit bounds.
- Custom classes are registered by server application code at startup. Clients discover metadata, create objects, retrieve attributes, and invoke methods. Runtime upload of executable code is outside scope.
- Storage lasts for the server process lifetime. Objects and trivial values belong to the creating session and are released on disconnect.
- Legacy wire compatibility, restart recovery, a GUI, and a public network service are outside scope. The client CLI provides reproducible demonstrations.

## Planned workflow

Each work item starts on a new branch. Implement one tested slice at a time, document its behavior, and stage only files intended for submission. Commits remain a manual action. See the plan for milestones and completion criteria.
