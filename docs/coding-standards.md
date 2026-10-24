# C++ coding standards and validation

## References and project policy

Use the [ISO C++ FAQ on coding standards](https://isocpp.org/wiki/faq/coding-standards) and the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) as references. They are guidance, not an ISO-mandated formatter preset or a complete mechanically enforceable standard. The language build mode is C++20.

PipeForge chooses a documented house style for consistent maintenance. Formatting, semantic analysis, runtime instrumentation, and review serve different purposes; none replaces the others. Upstream/generated code retains its own conventions.

## Layout and naming

`.clang-format` uses LLVM defaults with explicit C++ overrides. LLVM is a formatting preset, unrelated to JVM. The project chooses Stroustrup brace layout, four spaces without tabs, 100 columns, multiline nonempty function bodies, explicit control-flow braces, left-aligned pointer/reference declarators, grouped includes, and namespace end comments. Const placement remains a source-level decision: prefer conventional `const T&` declarations. These choices follow the naming/layout guidance while fixing details that the project must select itself.

Project namespaces, types, functions, parameters, and variables use lower_case with underscores. Macros use UPPER_CASE. Keep foreign APIs and GoogleTest-generated names unchanged. Explain intent and non-obvious ownership/lifetime constraints in comments; document public API parameters, results, errors, and side effects.

Headers use conventional, project-prefixed include guards rather than non-standard `#pragma once`, including the generated-version template.

Prefer RAII, scoped values, standard containers, bounded views, and explicit error contracts. Avoid owning raw pointers, unchecked pointer arithmetic, casts that discard type safety, uninitialized values, and implicit narrowing. Separate wire data, domain ownership, and I/O. Use narrow exception translation for throwing I/O operations that can be recovered or reported; do not wrap ordinary status-returning APIs in catch-all helpers.

The formatter runs with clang-format 19.1.5 and checks maintained C++ files, the generated-version header template and `.proto` schemas (two-space Google Proto style). clang-tidy enforces braces and naming as well as semantic rules. Formatting alone does not enforce SOLID, contracts, thread safety, ownership, or all Core Guidelines.

Use one empty line after a complete braced `if`/`else`, loop, `switch`, or `try`/`catch` chain before subsequent code or commentary. Keep branch continuations together and do not add an empty line before an enclosing closing brace. Function definitions are separated by one empty line. clang-format preserves these gaps but has no general control-block gap insertion option in version 19; `scripts/control_spacing.py` complements it using masked tokens, excluding literals/comments/preprocessor definitions. `python scripts/check_format.py --fix` applies both rules, and the same script without `--fix` validates them in CI. Tests cover chains, loops, nested closes, raw strings, macro definitions, and idempotence.

## clang-tidy and static analysis

`.clang-tidy` enables:

| Family | Intended coverage |
| --- | --- |
| `clang-analyzer-*` | Path-sensitive diagnostics including null dereference, uninitialized values, division by zero, allocation/deallocation misuse, use after free, and potential leaks. |
| `bugprone-*` | Suspicious lifetime, move, conversion, container, exception, and control-flow patterns. |
| `cppcoreguidelines-*` | Available automated Core Guidelines checks, including ownership, bounds, casts, initialization, and narrowing. |
| `cert-*`, `portability-*` | Additional safety/security and portability diagnostics. |
| `performance-*`, selected `modernize-*`/`readability-*` | Avoidable costs, explicit overrides/null pointers, ownership helpers, naming, and braces. |

All enabled diagnostics are errors. `scripts/check_tidy.py` verifies the configuration, reads the actual CMake `compile_commands.json`, deduplicates and checks every maintained translation unit, and restricts header diagnostics to this checkout. It fails if there are no selected sources. Third-party/generated implementation files are excluded, not globally suppressed compiler errors.

Clang's `N warnings generated.` totals include diagnostics that its header/system filters exclude from reporting. They are not counts of outstanding project findings. The runner preserves complete output in `build/<configuration>/clang-tidy.log` and prints every emitted diagnostic while moving only those standalone totals out of the console. CI retains the full log as the `clang-tidy-log` artifact for 14 days, including on analysis failure. Exit status and `WarningsAsErrors` remain unchanged; regression tests verify that real warnings/errors remain visible and a failed analyzer still fails the script. See the [clang-tidy header-filter and quiet options](https://clang.llvm.org/extra/clang-tidy/).

Use clang-tidy 19 or newer with a matching compiler/SDK/STL environment. For example, an old Clang parser cannot analyze a newer MSVC STL that requires a newer Clang; that is a failed analysis, not a warning to suppress. CI prints tool versions and uses the matching LLVM tools from its selected Windows installation.

`scripts/check_analysis_probes.py` analyzes intentionally defective temporary snippets without running them. CI requires specific diagnostics and a failing tool exit for leaks, use after free, division by zero, and uninitialized return. This checks that the analyzer configuration is active; it does not establish complete defect detection.

Reviewed exceptions:

- Disable `bugprone-easily-swappable-parameters`: the CLI deliberately accepts separate output/error streams of the same type. Semantic API review and tests cover their roles.
- Permit integer literals 0, 1, and 2 in the magic-number check: they represent common indexing/counts and the documented CLI status contract. Other meaningful constants should be named.
- The two C++ entry points locally suppress only `bugprone-exception-escape`. Fatal allocation failures during startup remain fatal rather than adding blanket exception recovery; recoverable throwing I/O still belongs at the I/O adapter. Bounds/ownership/analyzer checks remain enabled for those functions.
- Diagnostic RAII destructors use the same narrow exception-escape exception for unrecoverable allocation exhaustion. spdlog reports file write/flush errors through the session flag; `finish()` must be checked. Profiling macros locally exempt only `cppcoreguidelines-macro-usage` because they must capture lexical scope/function. Other checks remain active.

Protocol binary fixtures locally exempt magic-number warnings; the deterministic regression mutator locally exempts fixed-seed randomness checks because reproducibility is its purpose, not a security source. No production randomness policy is weakened. Benchmark/fuzz entry points use the fatal-allocation policy of other process entry points. Narrow pinned-upstream compiler exceptions are listed in [dependency review](third-party.md); maintained code retains its checks. The client's internal promise-completion helper has a documented exception-escape suppression: its mutex/closed-state invariants guarantee exactly-once completion, and a violated promise invariant is fatal rather than translated into an I/O failure.

Any additional exception needs a narrow named-check suppression with a reason and review. Do not disable an entire family to make a build green.

## Runtime safety

Whole-build Linux Clang ASan/UBSan instruments maintained code, generated messages,
Protobuf, protoc, Abseil, UTF-8, diagnostics and tests. Leak detection and findings
fail the job. A separate TSan build exercises portable client scheduling and
concurrent diagnostics; ASan and TSan are never combined. GCC remains covered by
normal compiler compatibility; GCC/UBSan is rejected because the pinned Abseil
triggers [upstream issue #1634](https://github.com/abseil/abseil-cpp/issues/1634).

Separate Linux libFuzzer runs add coverage-guided decoder testing with bounded
PR/master/weekly durations. Windows clang-cl ASan covers actual pipe/process
behavior. Native tests verify partial transfer, deadlines, cancellation/drain,
peer exit and shutdown. Linux instrumentation does not validate Win32 operations.
Safe ownership, bounded input, negative tests and review remain necessary.

## Validation and documentation

[The test guide](testing.md) defines CTest labels, the CI matrix, R1-R8 evidence and
platform limits. Each workflow keeps its own status; badge images filter
`branch=master&event=push`. End-to-end process tests run on Windows; Release also
checks extracted runtime ZIPs. All suites own their temporary state and use bounded
waits rather than relying on another job's artifacts.

Public interfaces use Doxygen comments describing purpose, ownership, concurrency,
expected failures, cancellation and side effects. Explain non-obvious invariants
near the code, especially native I/O lifetime and promise completion. Keep code and
guides in English. [Doxygen generation](api.md) fails on undocumented public members,
missing enum descriptions, incomplete supplied parameter docs and invalid references.
Private/generated/upstream internals are outside the public API reference.

Python and CMake files separate meaningful phases with blank lines and comments
that explain their intent. Keep control-flow continuations together and leave a
blank line after complete blocks before the next logical operation. Formatting
changes must preserve behavior.

See [LLVM 19 formatting options](https://releases.llvm.org/19.1.0/tools/clang/docs/ClangFormatStyleOptions.html)
and [clang-tidy checks](https://releases.llvm.org/19.1.0/tools/clang/tools/extra/docs/clang-tidy/checks/list.html)
for the supported configuration surface.
