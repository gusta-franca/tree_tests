# from __future__ import annotations

import argparse
import time

import pandas as pd

from src.python.metrics.adapted_paper_metrics import (
    mu_plus,
    reliable_fraction_of_information_prime_plus,
    g1,
    g2,
    g3_prime,
)

METRIC_FUNCS = {
    "mu_plus": mu_plus,
    "rfi_prime_plus": reliable_fraction_of_information_prime_plus,
    "g1": g1,
    "g2": g2,
    "g3_prime": g3_prime,
}

METRIC_RESULTS = {
    "mu_plus": "MuPlus",
    "rfi_prime_plus": "RfiPrimePlus",
    "g1": "G1Score",
    "g2": "G2Score",
    "g3_prime": "G3PrimeScore",
}

METRIC_FIELDS = {
    "is_key": "MuPlusIsKey",
    "lhs_uniqueness": "MuPlusLhsUniqueness",
    "lhs_size": "MuPlusLhsSize",
    "fi": "RfiFi",
    "bias_estimator": "RfiBiasEstimator",
}


def metric(name: str, value) -> None:
    print(f"Metric::{name}={value}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--metric", required=True, choices=sorted(METRIC_FUNCS))
    parser.add_argument("--csv", required=True)
    parser.add_argument("--lhs", required=True, help="'|'-separated LHS column names")
    parser.add_argument("--rhs", required=True)
    args = parser.parse_args()

    lhs_columns = args.lhs.split("|")

    load_start = time.perf_counter()
    df = pd.read_csv(args.csv)
    load_time_s = time.perf_counter() - load_start

    func = METRIC_FUNCS[args.metric]

    compute_start = time.perf_counter()
    result = func(df, lhs_columns, args.rhs)
    compute_time_s = time.perf_counter() - compute_start

    metric("LoadTimeSeconds", load_time_s)
    metric("ComputeTimeSeconds", compute_time_s)

    for key, value in result.items():
        if key == "result":
            metric(METRIC_RESULTS[args.metric], value)
        else:
            metric(METRIC_FIELDS.get(key, key), value)


if __name__ == "__main__":
    main()
