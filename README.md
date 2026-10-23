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

The repository contains the legacy Windows implementation and Visual Studio solution. Two incomplete, independent CMake definitions exist; there is no root CMake build, automated test suite, or CI workflow yet.

The [modernization plan](docs/modernization-plan.md) defines the proposed architecture, acceptance tests, security model, and implementation sequence. This change establishes the plan. The architecture and tooling below are targets for subsequent implementation, not completed features. The legacy sources were inspected but were not built or executed in this stage.

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

## Planned dependencies

- [Protocol Buffers](https://github.com/protocolbuffers/protobuf): reuse the official `protoc` generator and C++ runtime for binary serialization; implement only PipeForge schemas, validation, and domain conversion.
- [GoogleTest](https://github.com/google/googletest): reuse the upstream test framework with CTest, adding GoogleMock only where useful.

Dependencies will be built through CMake from pinned upstream sources. Generated code and downloaded sources remain in the build directory. The plan defines version matching, transitive dependencies, licensing, and reproducibility checks. Binary encoding is intended to reduce message overhead; performance gains will be measured rather than assumed.

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
