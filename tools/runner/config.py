"""Configuration loading, validation, and command construction."""

from __future__ import annotations

import hashlib
import json
import os
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import yaml


_DURATION = re.compile(r"^\s*(\d+(?:\.\d+)?)\s*(ms|s|m|h)\s*$", re.IGNORECASE)
_MEMORY = re.compile(r"^\s*(\d+(?:\.\d+)?)\s*(kb|mb|gb)\s*$", re.IGNORECASE)


class ConfigError(ValueError):
    pass


def parse_duration(value: Any) -> float:
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if value <= 0:
            raise ConfigError("timeout must be positive")
        return float(value)
    if not isinstance(value, str):
        raise ConfigError("timeout must be a number of seconds or a value such as 50ms, 10s, 5m, or 12h")
    match = _DURATION.match(value)
    if not match:
        raise ConfigError(f"invalid timeout {value!r}; use ms, s, m, or h")
    number = float(match.group(1))
    if number <= 0:
        raise ConfigError("timeout must be positive")
    factors = {"ms": 0.001, "s": 1.0, "m": 60.0, "h": 3600.0}
    return number * factors[match.group(2).lower()]


def parse_memory(value: Any) -> int | None:
    if value is None:
        return None
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if value <= 0:
            raise ConfigError("memory_limit must be positive")
        return int(value)
    if not isinstance(value, str):
        raise ConfigError("memory_limit must be null, bytes, or a value such as 512MB or 8GB")
    match = _MEMORY.match(value)
    if not match:
        raise ConfigError(f"invalid memory limit {value!r}; use KB, MB, or GB")
    number = float(match.group(1))
    if number <= 0:
        raise ConfigError("memory_limit must be positive")
    factors = {"kb": 1024, "mb": 1024**2, "gb": 1024**3}
    return int(number * factors[match.group(2).lower()])


def _positive_integer(value: Any, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise ConfigError(f"{name} must be a positive integer")
    return value


def _slug(value: str) -> str:
    slug = re.sub(r"[^A-Za-z0-9_.-]+", "-", value).strip("-.")
    return slug or "experiment"


def _argument_tokens(args: Any) -> list[str]:
    if args is None:
        return []
    if isinstance(args, list):
        return [str(item) for item in args]
    if not isinstance(args, dict):
        raise ConfigError("args must be a mapping or a list")

    tokens: list[str] = []
    for key, value in args.items():
        option = "--" + str(key).replace("_", "-")
        if value is None or value is False:
            continue
        if value is True:
            tokens.append(option)
        elif isinstance(value, list):
            for item in value:
                tokens.extend((option, str(item)))
        else:
            tokens.extend((option, str(value)))
    return tokens


def _resolve_executable(executable: str, working_directory: Path) -> str:
    """Add the native Windows suffix when the YAML uses a portable name."""
    if os.name != "nt" or Path(executable).suffix.lower() == ".exe":
        return executable
    candidate = Path(executable)
    if not candidate.is_absolute():
        candidate = working_directory / candidate
    if candidate.with_suffix(candidate.suffix + ".exe").exists():
        return executable + ".exe"
    return executable


@dataclass(frozen=True)
class RunSpec:
    run_id: str
    run_directory_name: str
    experiment: str
    repetition: int
    executable: str
    command: list[str]
    args: Any
    stdin: str
    tags: dict[str, Any]
    timeout_text: str
    timeout_seconds: float
    memory_limit_text: str | None
    memory_limit_bytes: int | None
    working_directory: Path
    results_directory: Path
    environment: dict[str, str]


def load_runs(config_path: Path) -> list[RunSpec]:
    config_path = config_path.resolve()
    try:
        raw = yaml.safe_load(config_path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ConfigError(f"configuration file not found: {config_path}") from exc
    except yaml.YAMLError as exc:
        raise ConfigError(f"invalid YAML in {config_path}: {exc}") from exc

    if not isinstance(raw, dict):
        raise ConfigError("the YAML root must be a mapping")
    experiments = raw.get("experiments")
    if not isinstance(experiments, list) or not experiments:
        raise ConfigError("experiments must be a non-empty list")

    config_dir = config_path.parent
    default_repetitions = _positive_integer(raw.get("repetitions", 1), "repetitions")
    default_timeout = raw.get("timeout", "12h")
    default_memory = raw.get("memory_limit")
    working_directory = (config_dir / raw.get("working_directory", ".")).resolve()
    results_directory = (config_dir / raw.get("results_directory", "results")).resolve()
    global_environment = raw.get("environment", {})
    if not isinstance(global_environment, dict):
        raise ConfigError("environment must be a mapping")

    runs: list[RunSpec] = []
    names: set[str] = set()
    for experiment in experiments:
        if not isinstance(experiment, dict):
            raise ConfigError("each experiment must be a mapping")
        name = experiment.get("name")
        executable = experiment.get("executable")
        if not isinstance(name, str) or not name.strip():
            raise ConfigError("each experiment needs a non-empty name")
        if name in names:
            raise ConfigError(f"duplicate experiment name: {name}")
        names.add(name)
        if not isinstance(executable, str) or not executable.strip():
            raise ConfigError(f"experiment {name!r} needs an executable")

        executable = _resolve_executable(executable, working_directory)
        args = experiment.get("args", {})
        stdin_text = str(experiment.get("stdin", "") or "")
        tags = experiment.get("tags", {})
        if not isinstance(tags, dict):
            raise ConfigError(f"tags for {name!r} must be a mapping")
        repetitions = _positive_integer(experiment.get("repetitions", default_repetitions), f"repetitions for {name!r}")
        timeout_text = str(experiment.get("timeout", default_timeout))
        timeout_seconds = parse_duration(experiment.get("timeout", default_timeout))
        memory_raw = experiment.get("memory_limit", default_memory)
        memory_text = None if memory_raw is None else str(memory_raw)
        memory_bytes = parse_memory(memory_raw)
        experiment_environment = experiment.get("environment", {})
        if not isinstance(experiment_environment, dict):
            raise ConfigError(f"environment for {name!r} must be a mapping")
        environment = {str(k): str(v) for k, v in {**global_environment, **experiment_environment}.items()}
        command = [executable, *_argument_tokens(args)]

        for repetition in range(1, repetitions + 1):
            identity = {
                "name": name,
                "repetition": repetition,
                "executable": executable,
                "args": args,
                "stdin": stdin_text,
                "tags": tags,
                "timeout": timeout_text,
                "memory_limit": memory_text,
                "working_directory": str(working_directory),
                "environment": environment,
            }
            digest = hashlib.sha256(json.dumps(identity, sort_keys=True, default=str).encode()).hexdigest()[:10]
            run_id = f"{_slug(name)}__rep{repetition:03d}__{digest}"
            runs.append(
                RunSpec(
                    run_id=run_id,
                    run_directory_name=run_id,
                    experiment=name,
                    repetition=repetition,
                    executable=executable,
                    command=command,
                    args=args,
                    stdin=stdin_text,
                    tags=tags,
                    timeout_text=timeout_text,
                    timeout_seconds=timeout_seconds,
                    memory_limit_text=memory_text,
                    memory_limit_bytes=memory_bytes,
                    working_directory=working_directory,
                    results_directory=results_directory,
                    environment=environment,
                )
            )
    return runs
