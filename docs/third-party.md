# Third-party dependencies and tools

| Dependency | Pinned revision/version | Use | License |
| --- | --- | --- | --- |
| GoogleTest | v1.17.0, `52eb8108c5bdec04579160ae17225d66034bd723` | Test-only, source fetched into the build tree; no external optional dependencies enabled | [BSD-3-Clause, upstream notice](https://github.com/google/googletest/blob/52eb8108c5bdec04579160ae17225d66034bd723/LICENSE) |
| clang-format | 19.1.5 | Formatting tool, not linked or shipped | [LLVM license](https://llvm.org/LICENSE.txt) |
| actions/checkout | v4.2.2, `11bd71901bbe5b1630ceea73d27597364c9af683` | CI source checkout | [MIT](https://github.com/actions/checkout/blob/11bd71901bbe5b1630ceea73d27597364c9af683/LICENSE) |
| actions/upload-artifact | v4.6.2, `ea165f8d65b6e75b540449e92b4886f43607fa02` | Pass release ZIPs between jobs | [MIT](https://github.com/actions/upload-artifact/blob/ea165f8d65b6e75b540449e92b4886f43607fa02/LICENSE) |
| actions/download-artifact | v4.3.0, `d3f86a106a0bac45b974a628896c90dbdf5c8093` | Retrieve tested ZIPs for publishing | [MIT](https://github.com/actions/download-artifact/blob/d3f86a106a0bac45b974a628896c90dbdf5c8093/LICENSE) |

No upstream source is copied into the repository. Source acquisition preserves upstream license files in the build tree. M1 executable packages do not contain GoogleTest or these tools. MSVC/Windows SDK and the platform C++ runtime are supplied under their respective vendor terms; review distribution obligations whenever packaged dependencies change.

Protobuf, protoc, and compatible transitive dependencies will be selected and pinned during M2. Record their licenses and any required binary-distribution notices when introducing them. Do not interpret this document as a license grant for PipeForge itself.
