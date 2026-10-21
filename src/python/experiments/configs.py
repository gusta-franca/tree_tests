"""
Generate yaml configuration files for each experiment
"""

from __future__ import annotations

import argparse
import ast
import os
from pathlib import Path
from typing import Any, Dict, List

import pandas as pd
import yaml

from src.python.benchmark.benchmark import get_dataset_path, violation_rate, numeric_type
from src.python.experiments.utils import format_violation_rows, scenario_tags, LHS_JOIN, NAIVE_METRICS, PYTHON_METRICS, PYTHON_EXECUTABLE
 
def naive_experiments(name_prefix: str, datapath: str, lhs_columns, rhs_column: str, tags: Dict[str, Any]) -> List[Dict[str, Any]]:
    experiments = []

    for binary, impl_tag in NAIVE_METRICS.items():
        experiments.append({
            "name": f"{name_prefix}__{binary}",
            "executable": f"build/bin/{binary}",
            "args": [datapath, LHS_JOIN.join(lhs_columns), rhs_column],
            "stdin": "",
            "tags": {**tags, "implementation": impl_tag},
        })
    
    return experiments


def python_experiments(name_prefix: str, datapath: str, lhs_columns, rhs_column: str, tags: Dict[str, Any]) -> List[Dict[str, Any]]:
    experiments = []

    for metric_name in PYTHON_METRICS:
        experiments.append({
            "name": f"{name_prefix}__python_{metric_name}",
            "executable": PYTHON_EXECUTABLE,
            "args": ["-m", "src.python.metrics.run_metric_cli",
                     "--metric", metric_name,
                     "--csv", datapath,
                     "--lhs", LHS_JOIN.join(lhs_columns),
                     "--rhs", rhs_column],
            "stdin": "",
            "tags": {**tags, "implementation": f"python_{metric_name}"},
        })

    return experiments

def synthetic_experiments(scenarios: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
    """
    One experiment per (scenario, implementation) combination. 
    Dirty mode only (no ground truth for syn data).
    """

    experiments = []

    for scenario in scenarios:
        datapath = str(get_dataset_path(scenario))
        lhs_columns = [f"lhs_{i}" for i in range(scenario["lhs_number"])]
        rhs_column = "rhs"
        tags = scenario_tags(scenario)

        experiments.append({
            "name": f"{scenario['name']}__ankerl_xxhash",
            "executable": "build/bin/ankerl_test",
            "args": [datapath, LHS_JOIN.join(lhs_columns), rhs_column, "xxhash", "dirty"],
            "stdin": "",
            "tags": {**tags, "implementation": "ankerl_xxhash", "mode": "dirty"},
        })

        experiments.append({
            "name": f"{scenario['name']}__auto_relate",
            "executable": "build/bin/auto_relate_test",
            "args": [datapath, LHS_JOIN.join(lhs_columns), rhs_column, "dirty"],
            "stdin": "",
            "tags": {**tags, "implementation": "auto_relate", "mode": "dirty"},
        })

        experiments += naive_experiments(scenario["name"], datapath, lhs_columns, rhs_column, {**tags, "mode": "dirty"})

        experiments += python_experiments(scenario["name"], datapath, lhs_columns, rhs_column, {**tags, "mode": "dirty"})
        
        # !!append rest

    return experiments


def fd_ground_truth_experiments(fd_filepath: str, data_type: str, violation_threshold: float = 0.05) -> List[Dict[str, Any]]:
    """
    One experiment per (candidate, implementation) configuration
    """

    filename = "clean_data.csv" if data_type == "clean_data" else "dirty_data_mix_0.1.csv"
    mode = "clean" if data_type == "clean_data" else "dirty"

    experiments = []
    case_ids = sorted(
        case_id for case_id in os.listdir(fd_filepath)
        if os.path.isdir(os.path.join(fd_filepath, case_id))
    )

    for case_id in case_ids:
        case_dir = os.path.join(fd_filepath, case_id)
        filepath = os.path.join(case_dir, filename)
        gt_path = os.path.join(case_dir, "ground_truth.csv")

        if not os.path.exists(filepath) or not os.path.exists(gt_path):
            print(f"Skipping {case_id}: missing {filename} or ground_truth.csv")
            continue

        df = pd.read_csv(filepath)
        gt_df = pd.read_csv(gt_path)

        for _, candidate in gt_df.iterrows():
            left_col = candidate["left_col"]
            right_col = candidate["right_col"]
            sample_type = candidate["sample_type"]
            violation_rows_raw = candidate["violation_rows"]
            violation_rows_list = (
                ast.literal_eval(violation_rows_raw)
                if isinstance(violation_rows_raw, str) else violation_rows_raw
            )

            if (sample_type == "N" and
                (violation_rate(len(df), violation_rows_list, violation_threshold) or
                 numeric_type(df, left_col, right_col))):
                continue

            v_rows_str = format_violation_rows(str(violation_rows_list))
            base_name = f"{case_id}__{left_col}__{right_col}__{sample_type}"
            tags = {
                "source": "real",
                "case_id": case_id,
                "left_col": left_col,
                "right_col": right_col,
                "sample_type": sample_type,
                "mode": mode,
                "data_type": data_type,
            }

            experiments.append({
                "name": f"{base_name}__ankerl_xxhash",
                "executable": "build/bin/ankerl_test",
                "args": [filepath, left_col, right_col, "xxhash", mode],
                "stdin": v_rows_str,
                "tags": {**tags, "implementation": "ankerl_xxhash"},
            })

            experiments.append({
                "name": f"{base_name}__auto_relate",
                "executable": "build/bin/auto_relate_test",
                "args": [filepath, left_col, right_col, mode],
                "stdin": v_rows_str,
                "tags": {**tags, "implementation": "auto_relate"},
            })

            experiments += naive_experiments(base_name, filepath, [left_col], right_col, tags)

            experiments += python_experiments(base_name, filepath, [left_col], right_col, tags)
            
            # !!append rest

    return experiments


def write_config(path: Path, experiments: List[Dict[str, Any]], results_dir_name: str, repetitions: int, timeout: str) -> None:
    project_root = Path.cwd().resolve()

    config = {
        "working_directory": str(project_root),
        "results_directory": str(project_root / "results" / results_dir_name),
        "repetitions": repetitions,
        "timeout": timeout,
        "experiments": experiments,
    }

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        yaml.safe_dump(config, f, sort_keys=False)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--synthetic", action="store_true", help="include synthetic scenarios")
    parser.add_argument("--fd", action="store_true", help="include data/FD ground-truth candidates")
    parser.add_argument("--data-type", choices=["clean_data", "dirty_data"], default="clean_data")
    parser.add_argument("--output", type=Path, default=Path("configs/generated.yaml"))
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--timeout", default="2m")
    args = parser.parse_args()

    if not args.synthetic and not args.fd:
        parser.error("choose at least one of --synthetic or --fd")

    experiments: List[Dict[str, Any]] = []

    if args.synthetic:
        from src.python.experiments.scenarios import scenarios
        experiments += synthetic_experiments(scenarios)

    if args.fd:
        experiments += fd_ground_truth_experiments("data/FD", args.data_type)

    write_config(args.output, experiments, results_dir_name=args.output.stem, repetitions=args.repetitions, timeout=args.timeout)

    print(f"Wrote {len(experiments)} experiments to {args.output}")
    print(f"Run: python3 -m tools.runner {args.output}")


if __name__ == "__main__":
    main()
