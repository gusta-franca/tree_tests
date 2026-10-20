"""Subprocess isolation, timeout enforcement, logging, and memory monitoring."""

from __future__ import annotations

import os
import signal
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import TextIO

from .config import RunSpec
from .metrics import parse_metrics


try:
    import psutil  # type: ignore
except ImportError:  # Linux has a dependency-free /proc fallback.
    psutil = None


def _linux_memory_guard(limit_bytes: int | None):
    """Return a Linux-only child allocation guard used as a monitoring backstop."""
    if not sys.platform.startswith("linux") or limit_bytes is None:
        return None

    def apply_limit() -> None:
        import resource

        resource.setrlimit(resource.RLIMIT_DATA, (limit_bytes, limit_bytes))

    return apply_limit


@dataclass
class ExecutionResult:
    status: str
    exit_code: int | None
    wall_time_ms: float
    peak_memory_mb: float | None
    started_at_utc: str
    ended_at_utc: str
    metrics: dict[str, object]
    error: str


def _timestamp() -> str:
    return datetime.now(timezone.utc).isoformat()


def _reader(stream: TextIO, destination: TextIO, captured: list[str]) -> None:
    try:
        for line in iter(stream.readline, ""):
            destination.write(line)
            destination.flush()
            captured.append(line)
    finally:
        stream.close()


def _linux_process_group_rss_bytes(process_group_id: int) -> int | None:
    proc = Path("/proc")
    if not proc.exists():
        return None
    page_size = os.sysconf("SC_PAGE_SIZE")
    total = 0
    found = False
    for entry in proc.iterdir():
        if not entry.name.isdigit():
            continue
        try:
            stat = (entry / "stat").read_text(encoding="utf-8")
            tail = stat[stat.rfind(")") + 2 :].split()
            pgrp = int(tail[2])
            rss_pages = int(tail[21])
        except (FileNotFoundError, PermissionError, ValueError, IndexError):
            continue
        if pgrp == process_group_id:
            total += max(0, rss_pages) * page_size
            found = True
    return total if found else None


def _process_tree_rss_bytes(process: subprocess.Popen[str]) -> int | None:
    if psutil is not None:
        try:
            root = psutil.Process(process.pid)
            processes = [root, *root.children(recursive=True)]
            return sum(item.memory_info().rss for item in processes if item.is_running())
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            return None
    if sys.platform.startswith("linux"):
        return _linux_process_group_rss_bytes(process.pid)
    return None


def _terminate_process_tree(process: subprocess.Popen[str], grace_seconds: float = 1.0) -> None:
    if process.poll() is not None:
        return
    if psutil is not None:
        try:
            root = psutil.Process(process.pid)
            children = root.children(recursive=True)
            for item in children:
                item.terminate()
            root.terminate()
            _, alive = psutil.wait_procs([*children, root], timeout=grace_seconds)
            for item in alive:
                item.kill()
            return
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass

    if os.name == "posix":
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            return
        try:
            process.wait(timeout=grace_seconds)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
    else:
        process.terminate()
        try:
            process.wait(timeout=grace_seconds)
        except subprocess.TimeoutExpired:
            process.kill()


def execute(spec: RunSpec, run_directory: Path) -> ExecutionResult:
    run_directory.mkdir(parents=True, exist_ok=True)
    stdout_path = run_directory / "stdout.log"
    stderr_path = run_directory / "stderr.log"
    started_at = _timestamp()
    start = time.monotonic()
    stdout_lines: list[str] = []
    stderr_lines: list[str] = []
    peak_memory = 0
    memory_observed = False
    status = "FAILED"
    error = ""
    process: subprocess.Popen[str] | None = None

    if spec.memory_limit_bytes is not None and psutil is None and not sys.platform.startswith("linux"):
        error = "memory_limit requires psutil on macOS and Windows; install requirements.txt"
        stdout_path.write_text("", encoding="utf-8")
        stderr_path.write_text(error + "\n", encoding="utf-8")
        ended_at = _timestamp()
        return ExecutionResult(
            status="FAILED",
            exit_code=None,
            wall_time_ms=round((time.monotonic() - start) * 1000.0, 3),
            peak_memory_mb=None,
            started_at_utc=started_at,
            ended_at_utc=ended_at,
            metrics={},
            error=error,
        )

    environment = os.environ.copy()
    environment.update(spec.environment)
    environment["EXPERIMENT_RUN_ID"] = spec.run_id
    environment["EXPERIMENT_REPETITION"] = str(spec.repetition)

    creation_flags = 0
    start_new_session = os.name == "posix"
    if os.name == "nt":
        creation_flags = subprocess.CREATE_NEW_PROCESS_GROUP  # type: ignore[attr-defined]

    with stdout_path.open("w", encoding="utf-8", buffering=1) as stdout_file, stderr_path.open(
        "w", encoding="utf-8", buffering=1
    ) as stderr_file:
        try:
            process = subprocess.Popen(
                spec.command,
                cwd=spec.working_directory,
                env=environment,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1,
                start_new_session=start_new_session,
                creationflags=creation_flags,
                # preexec_fn is deliberately avoided on macOS. In particular,
                # setting RLIMIT_DATA there can make Popen fail before exec.
                preexec_fn=_linux_memory_guard(spec.memory_limit_bytes),
            )
            assert process.stdout is not None and process.stderr is not None
            
            try:
                if process.stdin is not None:
                    process.stdin.write(spec.stdin)
                    process.stdin.close()
            except (BrokenPipeError, OSError):
                pass

            out_thread = threading.Thread(target=_reader, args=(process.stdout, stdout_file, stdout_lines), daemon=True)
            err_thread = threading.Thread(target=_reader, args=(process.stderr, stderr_file, stderr_lines), daemon=True)
            out_thread.start()
            err_thread.start()

            while True:
                exit_code = process.poll()
                rss = _process_tree_rss_bytes(process)
                if rss is not None:
                    memory_observed = True
                    peak_memory = max(peak_memory, rss)

                elapsed = time.monotonic() - start
                if exit_code is not None:
                    status = "SUCCESS" if exit_code == 0 else "FAILED"
                    if exit_code != 0:
                        error = f"process exited with code {exit_code}"
                    break
                if spec.memory_limit_bytes is not None and rss is not None and rss > spec.memory_limit_bytes:
                    status = "MEMORY_LIMIT"
                    error = f"memory usage {rss / 1024**2:.2f} MB exceeded limit {spec.memory_limit_bytes / 1024**2:.2f} MB"
                    _terminate_process_tree(process)
                    break
                if elapsed >= spec.timeout_seconds:
                    status = "TIMEOUT"
                    error = f"wall-clock timeout of {spec.timeout_text} exceeded"
                    _terminate_process_tree(process)
                    break
                time.sleep(min(0.01, max(0.001, spec.timeout_seconds - elapsed)))

            process.wait()
            out_thread.join(timeout=2.0)
            err_thread.join(timeout=2.0)
            if status == "FAILED" and spec.memory_limit_bytes is not None:
                stderr_text = "".join(stderr_lines).lower()
                memory_markers = ("bad_alloc", "cannot allocate memory", "out of memory", "memoryerror")
                if any(marker in stderr_text for marker in memory_markers):
                    status = "MEMORY_LIMIT"
                    error = f"process allocation failed under memory limit {spec.memory_limit_text}"
        except KeyboardInterrupt:
            status = "INTERRUPTED"
            error = "runner interrupted by user"
            if process is not None:
                _terminate_process_tree(process)
                process.wait()
        except FileNotFoundError as exc:
            status = "FAILED"
            error = str(exc)
            stderr_file.write(error + "\n")
        except (OSError, subprocess.SubprocessError) as exc:
            status = "FAILED"
            error = str(exc)
            stderr_file.write(error + "\n")

    ended_at = _timestamp()
    wall_time_ms = (time.monotonic() - start) * 1000.0
    exit_code = process.returncode if process is not None else None
    return ExecutionResult(
        status=status,
        exit_code=exit_code,
        wall_time_ms=round(wall_time_ms, 3),
        peak_memory_mb=round(peak_memory / 1024**2, 3) if memory_observed else None,
        started_at_utc=started_at,
        ended_at_utc=ended_at,
        metrics=parse_metrics(stdout_lines),
        error=error,
    )
