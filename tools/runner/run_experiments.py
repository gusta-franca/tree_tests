"""CLI for running experiment configurations."""

from __future__ import annotations

import argparse
import csv
import json
import os
import shlex
import sys
from pathlib import Path
from typing import Any

from .config import ConfigError, RunSpec, load_runs
from .executor import execute


CORE_COLUMNS = [
    "run_id",
    "experiment",
    "repetition",
    "status",
    "wall_time_ms",
    "peak_memory_mb",
    "exit_code",
    "timeout",
    "memory_limit",
    "started_at_utc",
    "ended_at_utc",
    "command",
    "error",
]


def _atomic_json(path: Path, value: Any) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def _flatten_result(result: dict[str, Any]) -> dict[str, Any]:
    row = {key: result.get(key) for key in CORE_COLUMNS}
    for key, value in result.get("tags", {}).items():
        row[f"tag_{key}"] = value
    args = result.get("args", {})
    if isinstance(args, dict):
        for key, value in args.items():
            row[f"arg_{key}"] = json.dumps(value) if isinstance(value, (list, dict)) else value
    else:
        row["arguments"] = json.dumps(args)
    for key, value in result.get("metrics", {}).items():
        column = key if key not in row else f"metric_{key}"
        row[column] = value
    return row


def rebuild_csv(results_directory: Path) -> Path:
    rows: list[dict[str, Any]] = []
    for path in sorted((results_directory / "runs").glob("*/result.json")):
        try:
            rows.append(_flatten_result(json.loads(path.read_text(encoding="utf-8"))))
        except (json.JSONDecodeError, OSError):
            continue

    standard = list(CORE_COLUMNS)
    extra = sorted({key for row in rows for key in row if key not in standard})
    columns = standard + extra
    csv_path = results_directory / "results.csv"
    temporary = csv_path.with_suffix(".csv.tmp")
    with temporary.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, csv_path)
    return csv_path


def _configuration_record(spec: RunSpec) -> dict[str, Any]:
    return {
        "run_id": spec.run_id,
        "experiment": spec.experiment,
        "repetition": spec.repetition,
        "executable": spec.executable,
        "command": spec.command,
        "args": spec.args,
        "tags": spec.tags,
        "timeout": spec.timeout_text,
        "timeout_seconds": spec.timeout_seconds,
        "memory_limit": spec.memory_limit_text,
        "memory_limit_bytes": spec.memory_limit_bytes,
        "working_directory": str(spec.working_directory),
        "environment": spec.environment,
    }


def _result_record(spec: RunSpec, result: Any) -> dict[str, Any]:
    return {
        "run_id": spec.run_id,
        "experiment": spec.experiment,
        "repetition": spec.repetition,
        "status": result.status,
        "wall_time_ms": result.wall_time_ms,
        "peak_memory_mb": result.peak_memory_mb,
        "exit_code": result.exit_code,
        "timeout": spec.timeout_text,
        "memory_limit": spec.memory_limit_text,
        "started_at_utc": result.started_at_utc,
        "ended_at_utc": result.ended_at_utc,
        "command": shlex.join(spec.command),
        "error": result.error,
        "args": spec.args,
        "tags": spec.tags,
        "metrics": result.metrics,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run isolated command-line experiments from YAML.")
    parser.add_argument("config", type=Path, help="path to the YAML experiment configuration")
    parser.add_argument("--dry-run", action="store_true", help="show expanded runs without executing them")
    parser.add_argument("--force", action="store_true", help="rerun configurations that already have result.json")
    parser.add_argument("--experiment", action="append", help="run only the named experiment (repeatable)")
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = build_parser().parse_args(argv)
    try:
        runs = load_runs(arguments.config)
    except ConfigError as exc:
        print(f"Configuration error: {exc}", file=sys.stderr)
        return 2

    selected = set(arguments.experiment or [])
    if selected:
        runs = [run for run in runs if run.experiment in selected]
        missing = selected - {run.experiment for run in runs}
        if missing:
            print(f"Unknown experiment(s): {', '.join(sorted(missing))}", file=sys.stderr)
            return 2

    if not runs:
        print("No runs selected.")
        return 0

    if arguments.dry_run:
        for spec in runs:
            memory = spec.memory_limit_text or "unlimited"
            print(f"{spec.run_id}: {shlex.join(spec.command)} [timeout={spec.timeout_text}, memory={memory}]")
        return 0

    results_directory = runs[0].results_directory
    runs_directory = results_directory / "runs"
    runs_directory.mkdir(parents=True, exist_ok=True)
    interrupted = False

    for index, spec in enumerate(runs, start=1):
        run_directory = runs_directory / spec.run_directory_name
        result_path = run_directory / "result.json"
        if result_path.exists() and not arguments.force:
            print(f"[{index}/{len(runs)}] SKIP {spec.run_id} (result already exists)")
            continue

        run_directory.mkdir(parents=True, exist_ok=True)
        _atomic_json(run_directory / "config.json", _configuration_record(spec))
        (run_directory / "command.txt").write_text(shlex.join(spec.command) + "\n", encoding="utf-8")
        print(f"[{index}/{len(runs)}] RUN  {spec.run_id}", flush=True)
        result = execute(spec, run_directory)
        _atomic_json(result_path, _result_record(spec, result))
        csv_path = rebuild_csv(results_directory)
        print(f"[{index}/{len(runs)}] {result.status:<12} {result.wall_time_ms:.3f} ms", flush=True)
        if result.status == "INTERRUPTED":
            interrupted = True
            break

    csv_path = rebuild_csv(results_directory)
    print(f"Results: {csv_path}")
    return 130 if interrupted else 0


if __name__ == "__main__":
    raise SystemExit(main())

