# Third-party dependencies and tools

| Dependency | Pinned revision/version | Use | License |
| --- | --- | --- | --- |
| GoogleTest | v1.17.0, `52eb8108c5bdec04579160ae17225d66034bd723` | Test-only, source fetched into the build tree; no external optional dependencies enabled | [BSD-3-Clause, upstream notice](https://github.com/google/googletest/blob/52eb8108c5bdec04579160ae17225d66034bd723/LICENSE) |
| spdlog | v1.17.0, `79524ddd08a4ec981b7fea76afd08ee05f83755d` | Static runtime logging and trace sinks; archive SHA256 `c6dee844e35a54cc80e3ab76aae2310763e0b49c7397549dfa4d4657d9737138` | [MIT](https://github.com/gabime/spdlog/blob/79524ddd08a4ec981b7fea76afd08ee05f83755d/LICENSE) |
| fmt, bundled by spdlog | Fixed by the spdlog source revision above | Formatting backend, no separate external version resolution | [Upstream bundled notice](https://github.com/gabime/spdlog/blob/79524ddd08a4ec981b7fea76afd08ee05f83755d/include/spdlog/fmt/bundled/fmt.license.rst) |
| nlohmann/json | v3.12.0, `65ee68451d8eb2b5f3a30b410476ab83deb3289b` | Trace-file serializer, archive SHA256 `13ef31d691947940a08909f8e0772f1d7d68e5da1678ee812a49c4bb0c996b2f` | [MIT](https://github.com/nlohmann/json/blob/65ee68451d8eb2b5f3a30b410476ab83deb3289b/LICENSE.MIT) |
| clang-format | 19.1.5 | Formatting tool, not linked or shipped | [LLVM license](https://llvm.org/LICENSE.txt) |
| clang-tidy / Clang Static Analyzer | 19 or newer, matched to the installed compiler/SDK | Semantic checks and analyzer probes, not shipped | [LLVM license](https://llvm.org/LICENSE.txt) |
| Windows ASan toolchain | LLVM 22.1.3, official `LLVM-22.1.3-win64.exe`, SHA256 `72ca54c3b7940615776d8994c0b918a1d8c4981bdcf13b439eaa86a1bd477001` | CI-only compiler/runtime installation; not included in Release packages | [LLVM license at the pinned tag](https://github.com/llvm/llvm-project/blob/llvmorg-22.1.3/llvm/LICENSE.TXT) |
| Protocol Buffers / protoc | v36.2, `424e1b0a184baa5a3c75061a1febd5f60a0a3707` | Full static C++ runtime 7.36.2 and matching build-time compiler; archive SHA256 `626026222f38ad25d37f37ecc2d3bf04698addf92c9c5baa0e91710d92419a09` | [BSD-3-Clause](https://github.com/protocolbuffers/protobuf/blob/v36.2/LICENSE) |
| utf8_range | Bundled in the pinned Protobuf tree | Static UTF-8 validation; no independent network resolution | [MIT](https://github.com/protocolbuffers/protobuf/blob/v36.2/third_party/utf8_range/LICENSE) |
| Abseil | 20250512.1, `76bb24329e8bf5f39704eb10d21b9a80befa7c81` | Protobuf transitive dependency; hash-verified release archive SHA256 `9b7a064305e9fd94d124ffa6cc358592eb42b5da588fb4e07d09254aa40086db` | [Apache-2.0](https://github.com/abseil/abseil-cpp/blob/20250512.1/LICENSE) |
| actions/checkout | v6, `d23441a48e516b6c34aea4fa41551a30e30af803` | CI source checkout | [MIT](https://github.com/actions/checkout/blob/d23441a48e516b6c34aea4fa41551a30e30af803/LICENSE) |
| actions/upload-artifact | v4.6.2, `ea165f8d65b6e75b540449e92b4886f43607fa02` | Pass release ZIPs between jobs | [MIT](https://github.com/actions/upload-artifact/blob/ea165f8d65b6e75b540449e92b4886f43607fa02/LICENSE) |
| actions/upload-artifact | v6, `b7c566a772e6b6bfb58ed0dc250532a479d7789f` | Analyzer/fuzz diagnostic artifacts | [MIT](https://github.com/actions/upload-artifact/blob/b7c566a772e6b6bfb58ed0dc250532a479d7789f/LICENSE) |
| actions/download-artifact | v4.3.0, `d3f86a106a0bac45b974a628896c90dbdf5c8093` | Retrieve tested ZIPs for publishing | [MIT](https://github.com/actions/download-artifact/blob/d3f86a106a0bac45b974a628896c90dbdf5c8093/LICENSE) |

No upstream source is copied into the repository. Source acquisition preserves upstream license files in the build tree. CMake assembles the runtime dependency notices into `THIRD-PARTY-NOTICES.txt` and installs it alongside every component. Packages do not contain GoogleTest or development tools. MSVC/Windows SDK and the platform C++ runtime are supplied under their respective vendor terms; review distribution obligations whenever packaged dependencies change.

Protobuf, bundled utf8_range and Abseil license texts are appended to the installed runtime notices; protoc and upstream development libraries are not packaged. Abseil's archive tag is also locked by SHA256, so moved/changed contents fail acquisition. No upstream/generated source or binary is committed. Do not interpret this document as a license grant for PipeForge itself.

## Advisory review and updates

`python scripts/check_dependencies.py` verifies the five FetchContent inputs have
immutable Git revisions or SHA256-locked archives. `--online --report
build/dependency-audit.json` additionally queries [OSV](https://osv.dev/) using the
four source commits and Abseil's OSS-Fuzz package/version identity. The workflow
runs on pull requests, master pushes, weekly and on manual dispatch. Findings,
incomplete/paginated responses and service/network errors fail the audit; there is
no advisory allowlist. Reports preserve query identities, acquisition hashes and
the UTC lookup time. See [OSV's API](https://google.github.io/osv.dev/api/).

A local review on 2026-10-23 returned no advisories for those queries. This is not
a complete SBOM vulnerability scan or a guarantee that no vulnerability exists.
Bundled fmt/utf8_range are fixed by their containing source trees and are not
independent package-version queries; review their upstream changes when updating
spdlog/Protobuf. OSV indexing and commit/version coverage can be incomplete.
The audit does not cover runner OS packages, every compiler/tool advisory or
malicious upstream changes. All source/license changes still require review.

For an update, inspect upstream release/security notes and bundled/transitive
changes, verify the exact source revision and archive hash, update the license
inventory, regenerate matching protoc/runtime outputs, and run the compiler,
sanitizer, fuzzing and extracted-package checks. Explicit offline source overrides
bypass download verification and must be supplied at the reviewed revision.

## Reviewed build exceptions

Project warnings/analyzer families remain enabled. Pinned upstream code has narrow
exceptions in `cmake/Dependencies.cmake`: remove unsupported `/MP` from clang-cl
targets because Ninja owns parallelism; disable invalid `offsetof` only for
Protobuf/generated message layouts; suppress MSVC C4715 only in its exhaustive
parser/nonreturning JSON-unparser sources, C4624 only in the intentional dynamic
message union source, and C5287 only in libprotoc's upb C enum adapters. CMake 4's
upstream CMP0141 deprecation warning is disabled only inside dependency setup.
Downloaded/generated sources are not modified. Recheck every exception when pins change.

Documentation CI uses the Ubuntu runner's Doxygen package (minimum 1.9.8) and
records its version. Local HTML generation also supports upstream Doxygen 1.13.2.
These build tools are not linked or distributed in runtime ZIPs.
