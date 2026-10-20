"""Parsing for the line-oriented metric protocol used by experiment programs."""

from __future__ import annotations

import re
from typing import Any, Iterable


METRIC_PATTERN = re.compile(r"^Metric::([A-Za-z][A-Za-z0-9_.-]*)=(.*)$")


def infer_value(raw: str) -> Any:
    value = raw.strip()
    lowered = value.lower()
    if lowered == "true":
        return True
    if lowered == "false":
        return False

    try:
        return int(value)
    except ValueError:
        pass

    try:
        return float(value)
    except ValueError:
        return value


def parse_metrics(lines: Iterable[str]) -> dict[str, Any]:
    """Return the last value printed for each valid Metric::Name=value line."""
    metrics: dict[str, Any] = {}
    for line in lines:
        match = METRIC_PATTERN.match(line.strip())
        if match:
            metrics[match.group(1)] = infer_value(match.group(2))
    return metrics

