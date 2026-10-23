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
    arguments = parser.parse_args()
    policy = Path(__file__).resolve().parents[2] / "cmake/Sanitizers.cmake"
    compile_flags = {
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-fno-sanitize-recover=all",
    }
    link_flags = compile_flags - {"-fno-omit-frame-pointer"}
    # Check the real project too: moving the policy after FetchContent would
    # leave dependency commands inconsistent even if the small fixture passes.
    actual_commands = json.loads(
        (arguments.build / "compile_commands.json").read_text(encoding="utf-8"))
    if not actual_commands:
        raise RuntimeError("The production compile database must not be empty")
    for entry in actual_commands:
        for flag in compile_flags:
            if (flag in entry["command"]) != (arguments.enabled == "1"):
                raise RuntimeError(f"Inconsistent production compile policy ({flag}): {entry['file']}")
    with tempfile.TemporaryDirectory(prefix="pipeforge-sanitizers-") as directory:
        source = Path(directory) / "source"
        dependency = source / "dependency"
        dependency.mkdir(parents=True)
        (source / "main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
        (dependency / "runtime.cpp").write_text("void runtime() {}\n", encoding="utf-8")
        (dependency / "CMakeLists.txt").write_text(
            "add_library(runtime STATIC runtime.cpp)\n"
            "add_executable(protoc ../main.cpp)\n"
            "target_link_libraries(protoc PRIVATE runtime)\n", encoding="utf-8")
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
            "endforeach()\n", encoding="utf-8")
        for enabled in (False, True):
            build = Path(directory) / ("on" if enabled else "off")
            result = subprocess.run([
                arguments.cmake, "-S", str(source), "-B", str(build), "-G", "Ninja",
                f"-DCMAKE_CXX_COMPILER={arguments.compiler}",
                f"-DPIPEFORGE_SANITIZERS={'ON' if enabled else 'OFF'}",
            ], capture_output=True, text=True, timeout=45)
            if result.returncode:
                raise RuntimeError("CMake fixture configuration failed:\n" + result.stdout + result.stderr)
            commands = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
            if len(commands) != 3:
                raise RuntimeError("Fixture must cover the runtime, protoc and application")
            for entry in commands:
                for flag in compile_flags:
                    if (flag in entry["command"]) != enabled:
                        raise RuntimeError(f"Inconsistent sanitizer compile policy: {entry['file']}")
            for target in ("protoc", "client"):
                options = (build / f"{target}.link-options").read_text(encoding="utf-8").split(";")
                for flag in link_flags:
                    if (flag in options) != enabled:
                        raise RuntimeError(f"Inconsistent sanitizer link policy: {target}")
    print("Sanitizer ON/OFF compile and link inheritance verified for runtime, protoc and client")


if __name__ == "__main__":
    main()
