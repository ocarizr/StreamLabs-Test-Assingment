"""Run R1-R8 scenarios through separate server/client processes over real pipes."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
from queue import Queue, Empty
import subprocess
import tempfile
from threading import Thread
import uuid


def exercise(client: Path, server: Path, directory: Path, environment: dict | None = None) -> None:
    endpoint = rf"\\.\pipe\pipeforge-m5-{os.getpid()}-{uuid.uuid4().hex}"
    host = subprocess.Popen(
        [str(server), "--serve", "--endpoint", endpoint],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        cwd=directory,
        env=environment,
    )
    ready = Queue()
    reader = Thread(target=lambda: ready.put(host.stdout.readline()), daemon=True)
    reader.start()

    try:
        try:
            line = ready.get(timeout=10)
        except Empty as error:
            raise AssertionError("server never published readiness") from error

        assert line.startswith("READY "), (line, host.poll())

        def invoke(mode: str, *diagnostics: str) -> subprocess.CompletedProcess:
            return subprocess.run(
                [str(client), mode, "--endpoint", endpoint, *diagnostics],
                capture_output=True,
                text=True,
                timeout=30,
                cwd=directory,
                env=environment,
            )

        # Each mode exercises every operation; mixed sessions also run simultaneously.
        for mode in ("--demo-sync", "--demo-async", "--demo-mixed"):
            result = invoke(mode)
            assert result.returncode == 0 and "R1-R8 scenario passed" in result.stdout, result
            assert not result.stderr, result

        with ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(invoke, ("--demo-mixed", "--demo-mixed")))

        for result in results:
            assert result.returncode == 0 and not result.stderr, result

        # Keep payloads out of logs while retaining useful client timing categories.
        log = directory / "client.log"
        trace = directory / "client.trace.json"
        help_result = subprocess.run(
            [str(client), "--help"], capture_output=True, text=True, timeout=10, env=environment
        )
        options = ["--log-file", str(log)]

        if "Profiling: enabled" in help_result.stdout:
            options += ["--trace-file", str(trace)]

        result = invoke("--demo-mixed", *options)
        assert result.returncode == 0 and not result.stderr, result
        text = log.read_text(encoding="utf-8")
        assert "client response id=" in text or "Command completed" in text
        assert "café" not in text and "payload" not in text.lower()

        if trace.exists():
            events = json.loads(trace.read_text(encoding="utf-8"))["traceEvents"]
            assert {"client.connect", "client.send", "client.receive"}.issubset(
                {event["name"] for event in events}
            )

        unavailable = subprocess.run(
            [
                str(client),
                "--demo-sync",
                "--endpoint",
                endpoint + "-missing",
                "--timeout-ms",
                "100",
            ],
            capture_output=True,
            text=True,
            timeout=10,
            env=environment,
        )
        assert (
            unavailable.returncode == 1 and "Client connection failed" in unavailable.stderr
        ), unavailable

        host.stdin.write("stop\n")
        host.stdin.flush()
        stdout, stderr = host.communicate(timeout=10)
        assert host.returncode == 0 and "STOPPED" in stdout and not stderr, (stdout, stderr)
    finally:
        # Always reap only the process created above, including failed readiness paths.
        if host.poll() is None:
            host.kill()

        host.communicate(timeout=10)
        reader.join(timeout=10)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--server", type=Path, required=True)
    arguments = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="pipeforge-ipc-process-") as temporary:
        exercise(arguments.client.resolve(), arguments.server.resolve(), Path(temporary))


if __name__ == "__main__":
    main()
