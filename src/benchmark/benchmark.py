import ast
from datetime import datetime
import os
import pandas as pd
import time
import tracemalloc
from typing import Any, Dict, List

from src.metrics.python.adapted_paper_metrics import mu_plus, reliable_fraction_of_information_prime_plus
from src.metrics.python.mu_plus_opt import mu_plus_opt
from src.metrics.python.cpp_metrics import run_cpp_binary, format_violation_rows
from src.benchmark.plot import plot_rank_frequency
from src.generator.generator import generate_SYN 


def get_fields(stats: dict, fields: Dict[str, List[str]]) -> dict:
    extracted = {}

    for name, keys in fields.items():
        value = None
        
        for k in keys:
            if k in stats:
                value = stats[k]
                break
        
        extracted[name] = value
    
    return extracted

def get_dataset_path(scenario: Dict[str, Any]) -> str:
   
    syn_data_dir = "data/synthetic"
    
    os.makedirs(syn_data_dir, exist_ok = True)
              
    dist = scenario["dist_params"]
    n_type = dist["n_type"]
    noise = dist["noise"]
        
    filename = (f"{scenario["name"]}_{n_type}_{noise}.csv")
    return os.path.join(syn_data_dir, filename)


def get_generation_args(scenario: Dict[str, Any]) -> Dict[str, Any]:
    
    dist = scenario["dist_params"]
    
    return {
        "tuples": scenario["tuples"],
        "tuple_sel": scenario["tuple_sel"],
        "lhs_number": scenario["lhs_number"], 
        "rhs_sel": scenario.get("rhs_sel"),
        "lhs_dist_alpha": dist["lhs_dist_alpha"],
        "lhs_dist_beta": dist["lhs_dist_beta"],
        "rhs_dist_alpha": dist["rhs_dist_alpha"],
        "rhs_dist_beta": dist["rhs_dist_beta"],
        "noise": dist.get("noise", 0.01),
        "dist_type": dist["dist_type"],
        "n_type": dist["n_type"],
    }


def log_metadata(dataset_path: str, scenario: Dict[str, any]):
    
    metadata_file = "data/synthetic/metadata.csv"
    dist_params = scenario["dist_params"]
    
    entry = {
        "file_path": dataset_path,
        "scenario_name": scenario["name"],
        "tuples": scenario["tuples"],
        "tuple_sel": scenario["tuple_sel"],
        "lhs_number": scenario["lhs_number"],
        "rhs_sel": scenario["rhs_sel"],
        "dist_type": dist_params["dist_type"],
        "n_type": dist_params["n_type"],
        "noise": dist_params["noise"],
    }
    
    if os.path.exists(metadata_file):
        df_meta = pd.read_csv(metadata_file)
        
        # Replace current metadata.csv with the new metadata
        if dataset_path in df_meta["file_path"].values:
            df_meta = df_meta[df_meta["file_path"] != dataset_path]
            
        df_meta = pd.concat([df_meta, pd.DataFrame([entry])], ignore_index = True);
    else:
        df_meta = pd.DataFrame([entry])
        
    df_meta.to_csv(metadata_file, index = False)
    

def prepare_datasets(scenarios: List[Dict[str, Any]], regenerate: bool = False):
    
    for scenario in scenarios:
        dataset_path = get_dataset_path(scenario)
        
        if not os.path.exists(dataset_path) or regenerate:
            gen_args = get_generation_args(scenario)
            df = generate_SYN(gen_args)
            
            df.to_csv(dataset_path, index = False)
            print(f"Saved dataset on {dataset_path}")
            
        log_metadata(dataset_path, scenario)


def generate_dataset(scenario: Dict[str, Any]) -> None:

    filepath = get_dataset_path(scenario)
    generation_args = get_generation_args(scenario)
        
    df = generate_SYN(**generation_args)
    df.to_csv(filepath, index = False)


def load_dataset(filepath: str) -> pd.DataFrame:

    return pd.read_csv(filepath)
    

def save_results(results, prefix: str = "benchmark"):    
    results_dir = "results"
    os.makedirs(results_dir, exist_ok = True)
    
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = f"{prefix}_{timestamp}.csv"
    filepath = os.path.join(results_dir, filename)
    
    df = pd.DataFrame(results)
    df.to_csv(filepath, index = False)
    print(f"\nResults saved in {filepath}")

def run_metric(function, build_kwargs, field_map, call_args) -> dict:
    kwargs = build_kwargs(*call_args)
    stats = function(**kwargs)

    if not stats:
        return None
    
    return get_fields(stats, field_map)


def run_python_metric(metric_func, csv_filepath, lhs, rhs):
    
    load_start = time.time();
    df = pd.read_csv(csv_filepath);
    load_time = time.time() - load_start
    
    tracemalloc.start()
    start_time = time.time()
    
    result = metric_func(df = df, lhs = lhs, rhs = rhs)
    
    compute_time = time.time() - start_time
    _, memory_peak = tracemalloc.get_traced_memory()
    tracemalloc.stop()
    
    return {
        "result_value": result["result"],
        "load_time_s": load_time,
        "build_time_s": 0.0,  # Pandas builds and computes at the same time 
        "compute_time_s": compute_time,
        "memory_used_mb": memory_peak / (1024 * 1024)
    }


def python_metric_runner(metric_func, target_field: str, own_time_field: str):
    def run(csv_filepath, lhs, rhs):
        stats = run_python_metric(metric_func=metric_func, csv_filepath=csv_filepath, lhs=lhs, rhs=rhs)
        return {
            target_field: stats.get("result_value"),
            own_time_field: stats.get("compute_time_s", 0.0),
            "load_time_s": stats.get("load_time_s", 0.0),
            "build_time_s": stats.get("build_time_s", 0.0),
            "compute_time_s": stats.get("compute_time_s", 0.0),
            "memory_used_mb": stats.get("memory_used_mb", 0.0),
        }
    
    return run


def run_benchmarks(scenarios: List[Dict[str, Any]], metrics_config: List[Dict[str, Any]] = None) -> pd.DataFrame:
    if metrics_config is None:
        metrics_config = BENCHMARK_METRIC_CONFIGS

    results = []

    for scenario in scenarios:
        datapath = get_dataset_path(scenario)
        lhs_columns = [f"lhs_{i}" for i in range(scenario["lhs_number"])]
        rhs_column = "rhs"

        for config in metrics_config:
            print(f"Running scenario \"{scenario['name']}\" with \"{config['name']}\"\n")

            extracted = run_metric(config["function"], config["build_kwargs"], config["field_map"],
                                  (datapath, lhs_columns, rhs_column))
            if extracted is None:
                print(f"[{config['name']}] no result for scenario {scenario['name']}")
                continue

            row = {"scenario": scenario["name"], "implementation": config["name"], **extracted}

            for f in ("mu_time_s", "rfi_time_s", "auto_relate_time_s", "independence_time_s"):
                row[f] = row.get(f) or 0.0

            round_fields = ["mu_plus", "rfi_prime_plus", "score", "violation_rate",
                            "load_time_s", "build_time_s", "mu_time_s", "rfi_time_s",
                            "auto_relate_time_s", "independence_time_s",
                            "total_compute_time_s", "memory_used_mb"]
            
            for f in round_fields:
                if row.get(f) is not None:
                    row[f] = round(row[f], 5)

            row["total_time_s"] = round(
                (row.get("load_time_s") or 0.0) + 
                (row.get("build_time_s") or 0.0) + 
                (row.get("total_compute_time_s") or 0.0), 5
            )

            results.append(row)

    save_results(results)
    return results


def violation_rate(row_count: int, violation_rows: list, threshold: float = 0.05):
    return len(violation_rows) / row_count > threshold

    
def numeric_type(data: pd.DataFrame, left_col: str, right_col: str) -> bool:
    return (pd.api.types.is_numeric_dtype(data[left_col]) and pd.api.types.is_numeric_dtype(data[right_col]))


def run_metric_fd_ground_truth(
    metric_config: Dict[str, Any],
    fd_filepath: str = "data/FD",
    data_type: str = "clean_data",
) -> pd.DataFrame:

    if data_type not in ("clean_data", "dirty_data"):
        print(f"Enter a valid data_type, not {data_type}")
        return pd.DataFrame()

    filename = "clean_data.csv" if data_type == "clean_data" else "dirty_data_mix_0.1.csv"
    mode = "clean" if data_type == "clean_data" else "dirty"

    case_ids = sorted(
        case_id for case_id in os.listdir(fd_filepath)
        if os.path.isdir(os.path.join(fd_filepath, case_id))
    )

    rows = []

    for case_id in case_ids:
        case_dir = os.path.join(fd_filepath, case_id)
        filepath = os.path.join(case_dir, filename)
        gt_path = os.path.join(case_dir, "ground_truth.csv")

        if not os.path.exists(filepath) or not os.path.exists(gt_path):
            print(f"Skipping {case_id} for missing {filename} or ground_truth.csv")
            continue

        df = pd.read_csv(filepath)
        gt_df = pd.read_csv(gt_path)

        for _, candidate in gt_df.iterrows():
            left_col = candidate["left_col"]
            right_col = candidate["right_col"]
            sample_type = candidate["sample_type"]
            violation_rows = candidate["violation_rows"]

            violation_rows_list = ast.literal_eval(violation_rows) if isinstance(violation_rows, str) else violation_rows

            if (sample_type == 'N' and
                (violation_rate(len(df), violation_rows_list) or
                 numeric_type(df, left_col, right_col))):
                continue

            extracted = run_metric(metric_config["function"], metric_config["build_kwargs"],
                                    metric_config["field_map"],
                                    (filepath, left_col, right_col, violation_rows, mode))

            if extracted is None:
                print(f"[{metric_config['name']}] no result for {case_id}: {left_col} -> {right_col}")
                continue

            row = {
                "case_id": case_id,
                "left_col": left_col,
                "right_col": right_col,
                "sample_type": sample_type,
                "implementation": metric_config["name"],
            }

            row.update(extracted)            
            rows.append(row)

    results_df = pd.DataFrame(rows)
    save_results(results_df, prefix=f"fd_ground_truth_{data_type}_{metric_config['name']}")

    if "score" in results_df.columns:
        print_fd_ground_truth_metrics(results_df, threshold=metric_config.get("score_threshold", 0.5))

    return results_df
    
def run_benchmark_fd_ground_truth(
    metric_configs: List[Dict[str, Any]] = None,
    fd_filepath: str = "data/FD",
    data_type: str = "clean_data",
) -> pd.DataFrame:

    if metric_configs is None:
        metric_configs = FD_GROUND_TRUTH_METRIC_CONFIG

    all_results = []
    for config in metric_configs:
        print(f"\nRunning FD ground-truth benchmark: \"{config['name']}\" on {data_type}\n")
        all_results.append(run_metric_fd_ground_truth(config, fd_filepath, data_type))

    return pd.concat(all_results, ignore_index=True) if all_results else pd.DataFrame()

 
def print_fd_ground_truth_metrics(results_df: pd.DataFrame, threshold: float = 0.5):
    if results_df.empty:
        print("no results")
        return
 
    scored = results_df.dropna(subset = ["score"])
 
    predicted = scored["score"] <= threshold
    real = scored["sample_type"] == "P"
 
    tp = int((predicted & real).sum())
    fp = int((predicted & ~real).sum())
    fn = int((~predicted & real).sum())
 
    precision = tp / (tp + fp) if (tp + fp) > 0 else 0.0
    recall = tp / (tp + fn) if (tp + fn) > 0 else 0.0
    f1 = (2 * precision * recall / (precision + recall)) if (precision + recall) > 0 else 0.0
 
    print(f"\nAuto-Relate metrics (theshold = {threshold})")
    print(f"Candidates scored: {len(scored)} / {len(results_df)}")
    print(f"TP = {tp}, FP = {fp}, FN = {fn}")
    print(f"Precision, Recall, F1 = ({precision}, {recall}, {f1})")


AUTO_RELATE_FIELDS = {
    "score": ["score", "auto_relate_score"],
    "is_reliable": ["is_reliable", "auto_relate_is_reliable"],
    "violation_count": ["violation_count", "auto_relate_violation_count"],
    "violation_rate": ["violation_rate", "auto_relate_violation_rate"],
    "independence_pvalue": ["independence_pvalue"],
    "independence_used": ["independence_used"],
    "independence_rejected": ["independence_rejected"],
    "load_time_s": ["load_time_s"],
    "build_time_s": ["build_time_s"],
    "compute_time_s": ["compute_time_s"],
}

BENCHMARK_FIELDS = {
    "mu_plus": ["mu_plus"],
    "rfi_prime_plus": ["rfi_prime_plus"],
    "score": ["score", "auto_relate_score"],
    "is_reliable": ["is_reliable", "auto_relate_is_reliable"],
    "violation_count": ["violation_count", "auto_relate_violation_count"],
    "violation_rate": ["violation_rate", "auto_relate_violation_rate"],
    "independence_used": ["independence_used"],
    "independence_rejected": ["independence_rejected"],
    "independence_pvalue": ["independence_pvalue"],
    "load_time_s": ["load_time_s"],
    "build_time_s": ["build_time_s"],
    "mu_time_s": ["mu_time_s"],
    "rfi_time_s": ["rfi_time_s"],
    "auto_relate_time_s": ["auto_relate_time_s"],
    "independence_time_s": ["independence_time_s"],
    "total_compute_time_s": ["compute_time_s"],
    "memory_used_mb": ["memory_used_mb"],
}

BENCHMARK_METRIC_CONFIGS = [
    # {
    #     "name": "py_mu_plus",
    #     "function": python_metric_runner(mu_plus, "mu_plus", "mu_time_s"),
    #     "build_kwargs": lambda datapath, lhs, rhs: {"csv_filepath": datapath, "lhs": lhs, "rhs": rhs},
    #     "field_map": BENCHMARK_FIELDS,
    # },
    # {
    #     "name": "py_rfi_prime_plus",
    #     "function": python_metric_runner(reliable_fraction_of_information_prime_plus, "rfi_prime_plus", "rfi_time_s"),
    #     "build_kwargs": lambda datapath, lhs, rhs: {"csv_filepath": datapath, "lhs": lhs, "rhs": rhs},
    #     "field_map": BENCHMARK_FIELDS,
    # },
    {
        "name": "cpp_metrics_ankerl_xxhash",
        "function": run_cpp_binary,
        "build_kwargs": lambda datapath, lhs, rhs: {
            "binary_name": "ankerl_test",
            "args": [datapath, "|".join(lhs), rhs, "xxhash"],
            "stdin": "",
        },
        "field_map": BENCHMARK_FIELDS,
    },
    {
        "name": "cpp_auto_relate_syn",
        "function": run_cpp_binary,
        "build_kwargs": lambda datapath, lhs, rhs: {
            "binary_name": "auto_relate_test",
            "args": [datapath, "|".join(lhs), rhs, "dirty"],
            "stdin": "",
        },
        "field_map": BENCHMARK_FIELDS,
    },
]

FD_GROUND_TRUTH_METRIC_CONFIG = [
    {
        "name": "cpp_auto_relate_fd",
        "function": run_cpp_binary,
        "field_map": AUTO_RELATE_FIELDS,
        "build_kwargs": lambda filepath, left_col, right_col, violation_rows, mode: {
            "binary_name": "auto_relate_test",
            "args": [filepath, left_col, right_col, mode],
            "stdin": format_violation_rows(violation_rows),
        },
    },
    {
        "name": "cpp_metrics_ankerl",
        "function": run_cpp_binary,
        "field_map": AUTO_RELATE_FIELDS,
        "build_kwargs": lambda filepath, left_col, right_col, violation_rows, mode: {
            "binary_name": "ankerl_test",
            "args": [filepath, left_col, right_col, "xxhash", mode],
            "stdin": format_violation_rows(violation_rows),
        },
    },
]
