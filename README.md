# PipeForge

PipeForge is a C++ client/server project for typed interprocess communication and server-side object interaction over Windows named pipes.

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

M1 provides a root CMake build, separate client/server executable scaffolds, GoogleTest/CTest tests, formatting checks, initial GitHub Actions validation, and tag-triggered release packaging. The executables support `--help` and `--version`; starting IPC is deliberately unavailable until the later implementation milestones.

The Visual Studio solution and project files have been retired. Sources in `Client/` and `Server/` are retained only as legacy reference and are excluded from the new build, formatting checks, and release packages. Generated IDE/build outputs are no longer tracked.

The [modernization plan](docs/modernization-plan.md) defines the remaining architecture and acceptance criteria. Protobuf integration starts in M2, domain behavior in M3, and named-pipe transport in M4. See [build instructions](docs/build.md), [release instructions](docs/releases.md), and [dependency notices](docs/third-party.md).

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

The first test-enabled configure downloads pinned GoogleTest sources. Use `windows-release` for optimized builds or `windows-clang` for clang-cl. The portable scaffold can be built with `linux-debug`; this does not provide Linux named-pipe support. Formatting uses clang-format 19.1.5.

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

- [GoogleTest](https://github.com/google/googletest), v1.17.0 at revision `52eb8108c5bdec04579160ae17225d66034bd723`: test-only, built through CMake FetchContent and integrated with CTest.
- [Protocol Buffers](https://github.com/protocolbuffers/protobuf), planned for M2: reuse official `protoc` and the C++ runtime; implement only PipeForge schemas, validation, and domain conversion.

Downloaded sources and generated output remain in the build directory. The plan defines version matching, transitive dependencies, and reproducibility checks. Binary serialization efficiency will be measured rather than assumed.

## Releases

Pushing a version tag such as `V1.0`, `V1.0.0`, or `V1.0.1` publishes a release only if the tagged commit belongs to `master` history. Lowercase `v` is also accepted. The pipeline builds that exact commit and produces three Windows x64 ZIPs: client, server, and bundle. Each contains only the required executable runtime files. Ordinary branch pushes and pull requests never publish. See [release instructions](docs/releases.md) for examples and failure handling.

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
