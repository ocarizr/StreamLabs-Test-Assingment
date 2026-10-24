"""Check sanitizer inheritance without building or running instrumented binaries.

Generate a small CMake dependency tree with the production policy. Inspect the
actual compile commands and evaluated linker options for both option states.
This also runs on Windows, where the Linux sanitizer runtimes cannot execute.
"""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--enabled", required=True, choices=("0", "1"))
    parser.add_argument("--windows-asan", default="0", choices=("0", "1"))
    parser.add_argument("--tsan", default="0", choices=("0", "1"))
    arguments = parser.parse_args()
    policy = Path(__file__).resolve().parents[2] / "cmake/Sanitizers.cmake"
    compile_flags = {
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-fno-sanitize-recover=all",
    }
    link_flags = compile_flags - {"-fno-omit-frame-pointer"}
    thread_flags = {"-fsanitize=thread", "-fno-omit-frame-pointer"}

    # Check the real project too: moving the policy after FetchContent would
    # leave dependency commands inconsistent even if the small fixture passes.
    actual_commands = json.loads(
        (arguments.build / "compile_commands.json").read_text(encoding="utf-8")
    )

    if not actual_commands:
        raise RuntimeError("The production compile database must not be empty")

    for entry in actual_commands:
        if Path(entry["file"]).suffix.lower() not in (".c", ".cc", ".cpp", ".cxx"):
            continue  # Windows resource compiler commands cannot be instrumented.

        if ("/fsanitize=address" in entry["command"]) != (arguments.windows_asan == "1"):
            raise RuntimeError(f"Inconsistent Windows ASan compile policy: {entry['file']}")

        for flag in compile_flags:
            expected = arguments.enabled == "1" or (
                flag == "-fno-omit-frame-pointer"
                and (arguments.windows_asan == "1" or arguments.tsan == "1")
            )

            if (flag in entry["command"]) != expected:
                raise RuntimeError(
                    f"Inconsistent production compile policy ({flag}): {entry['file']}"
                )

        if ("-fsanitize=thread" in entry["command"]) != (arguments.tsan == "1"):
            raise RuntimeError(f"Inconsistent production TSan policy: {entry['file']}")

    # Build an isolated dependency/compiler/client graph for configure-only checks.
    with tempfile.TemporaryDirectory(prefix="pipeforge-sanitizers-") as directory:
        source = Path(directory) / "source"
        dependency = source / "dependency"
        dependency.mkdir(parents=True)
        (source / "main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
        (dependency / "runtime.cpp").write_text("void runtime() {}\n", encoding="utf-8")
        (dependency / "CMakeLists.txt").write_text(
            "add_library(runtime STATIC runtime.cpp)\n"
            "add_executable(protoc ../main.cpp)\n"
            "target_link_libraries(protoc PRIVATE runtime)\n",
            encoding="utf-8",
        )
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.25)\n"
            "project(SanitizerPolicy LANGUAGES CXX)\n"
            "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
            f'include("{policy.as_posix()}")\n'
            "add_subdirectory(dependency)\n"
            "add_executable(client main.cpp)\n"
            "target_link_libraries(client PRIVATE runtime)\n"
            "foreach(target IN ITEMS protoc client)\n"
            '  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/${target}.link-options"\n'
            '    CONTENT "$<TARGET_PROPERTY:${target},LINK_OPTIONS>")\n'
            "endforeach()\n",
            encoding="utf-8",
        )

        # Exercise each sanitizer policy against the same dependency graph.
        for mode in ("off", "linux", "windows", "thread"):
            enabled = mode == "linux"
            windows = mode == "windows"
            thread = mode == "thread"
            build = Path(directory) / mode
            resource = Path(directory) / f"resource-{mode}"
            libraries = (
                "clang_rt.asan_dynamic-x86_64.lib",
                "clang_rt.asan_static_runtime_thunk-x86_64.lib",
            )
            runtime = resource / "lib/windows"
            runtime.mkdir(parents=True)

            for library in libraries:
                (runtime / library).touch()  # Configure-only fixture, never linked.

            result = subprocess.run(
                [
                    arguments.cmake,
                    "-S",
                    str(source),
                    "-B",
                    str(build),
                    "-G",
                    "Ninja",
                    f"-DCMAKE_CXX_COMPILER={arguments.compiler}",
                    f"-DPIPEFORGE_SANITIZERS={'ON' if enabled else 'OFF'}",
                    f"-DPIPEFORGE_WINDOWS_ASAN={'ON' if windows else 'OFF'}",
                    f"-DPIPEFORGE_TSAN={'ON' if thread else 'OFF'}",
                    f"-DPIPEFORGE_ASAN_RESOURCE_DIR={resource.as_posix()}",
                ],
                capture_output=True,
                text=True,
                timeout=45,
            )

            if result.returncode:
                raise RuntimeError(
                    "CMake fixture configuration failed:\n" + result.stdout + result.stderr
                )

            # Inspect evaluated compile and link options for every fixture target.
            commands = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))

            if len(commands) != 3:
                raise RuntimeError("Fixture must cover the runtime, protoc and application")

            for entry in commands:
                for flag in ("/fsanitize=address", "/Zi"):
                    # /Zi may also be a compiler default; instrumentation must
                    # match the selected policy for every dependency/consumer.
                    if flag == "/Zi" and not windows:
                        continue

                    if (flag in entry["command"]) != windows:
                        raise RuntimeError(
                            f"Inconsistent Windows sanitizer policy ({flag}): {entry['file']}"
                        )

                for flag in compile_flags:
                    expected = enabled or (
                        (windows or thread) and flag == "-fno-omit-frame-pointer"
                    )

                    if (flag in entry["command"]) != expected:
                        raise RuntimeError(
                            f"Inconsistent sanitizer compile policy: {entry['file']}"
                        )

                for flag in thread_flags:
                    expected = thread or (
                        (enabled or windows) and flag == "-fno-omit-frame-pointer"
                    )

                    if (flag in entry["command"]) != expected:
                        raise RuntimeError(f"Inconsistent TSan compile policy: {entry['file']}")

            for target in ("protoc", "client"):
                options = (build / f"{target}.link-options").read_text(encoding="utf-8").split(";")

                if ("/INCREMENTAL:NO" in options) != windows:
                    raise RuntimeError(f"Inconsistent Windows sanitizer link policy: {target}")

                if ("/INFERASANLIBS:NO" in options) != windows:
                    raise RuntimeError(f"Inconsistent sanitizer runtime inference: {target}")

                for library in libraries:
                    if any(library in option for option in options) != windows:
                        raise RuntimeError(
                            f"Missing or unexpected sanitizer runtime ({library}): {target}"
                        )

                for flag in link_flags:
                    if (flag in options) != enabled:
                        raise RuntimeError(f"Inconsistent sanitizer link policy: {target}")

                if ("-fsanitize=thread" in options) != thread:
                    raise RuntimeError(f"Inconsistent TSan link policy: {target}")

    print(
        "Sanitizer OFF/ASan/UBSan/Windows/TSan inheritance verified for runtime, protoc and client"
    )


if __name__ == "__main__":
    main()
