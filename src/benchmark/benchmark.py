from datetime import datetime
import os
import pandas as pd
import subprocess
import time
import tracemalloc
from typing import Any, Callable, Dict, List, Tuple

from src.metrics.python.adapted_paper_metrics import mu_plus, reliable_fraction_of_information_prime_plus
from src.metrics.python.mu_plus_opt import mu_plus_opt
from src.metrics.python.cpp_metrics import cpp_metrics
from src.benchmark.plot import plot_rank_frequency
from src.generator.generator import generate_SYN 


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
    

def save_results(results: List[Dict[str, Any]]):
    
    results_dir = "results"
    os.makedirs(results_dir, exist_ok = True)
    
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = f"benchmark_{timestamp}.csv"
    filepath = os.path.join(results_dir, filename)
    
    df = pd.DataFrame(results)
    df.to_csv(filepath, index = False)
    print(f"\nResults saved in {filepath}")
    

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


def run_benchmarks(scenarios: List[Dict[str, Any]]) -> pd.DataFrame:
    
    metrics_config = [
        {"name": "py_mu_plus", "function": mu_plus, "is_cpp": False},
        {"name": "py_mu_plus_opt", "function": mu_plus_opt, "is_cpp": False},
        # {"name": "cpp_mu_plus_auto", "function": cpp_mu_plus_opt, "is_cpp": True, "binary_name": "fd_metrics_opt_test"},
        # {"name": "cpp_mu_plus_partitioned", "function": cpp_mu_plus_opt, "is_cpp": True, "binary_name": "fd_metrics_partitioned_test"},
        # {"name": "cpp_mu_plus_simd_murmur", "function": cpp_mu_plus_opt, "is_cpp": True, "binary_name": "bucketing_simd_test", "algo": "murmur"},
        # {"name": "cpp_mu_plus_simd_xxhash", "function": cpp_mu_plus_opt, "is_cpp": True, "binary_name": "bucketing_simd_test", "algo": "xxhash"},
        # {"name": "py_rfi_prime_plus", "function": reliable_fraction_of_information_prime_plus, "is_cpp": False},
        {"name": "cpp_metrics_simd_murmur", "function": cpp_metrics, "is_cpp": True, "binary_name": "bucketing_simd_test", "algo": "murmur"},
        {"name": "cpp_metrics_simd_xxhash", "function": cpp_metrics, "is_cpp": True, "binary_name": "bucketing_simd_test", "algo": "xxhash"},
        {"name": "cpp_metrics_ankerl_xxhash", "function": cpp_metrics, "is_cpp": True, "binary_name": "ankerl_test", "algo": "xxhash"},
        # {
        #     "name": "cpp_mu_plus_bitmap",
        #     "function": cpp_mu_plus_opt,
        #     "language": "cpp",
        #     "lhs": lhs_columns, 
        #     "rhs": "rhs",
        #     "algo": "bitmap"
        # },
        # {
        #     "name": "cpp_mu_plus_hash",
        #     "function": cpp_mu_plus_opt,
        #     "language": "cpp",
        #     "lhs": lhs_columns, 
        #     "rhs": "rhs",
        #     "algo": "hash"
        # },
    ]
    
    results = []
    
    for scenario in scenarios:        
        datapath = get_dataset_path(scenario)

        lhs_columns = [f"lhs_{i}" for i in range(scenario["lhs_number"])]
        rhs_column = "rhs"
        
        for config in metrics_config:            
            print(f"Running: {scenario["name"]}, {config["name"]}.\n")
            
            if config["is_cpp"]:
                stats = config["function"](
                    csv_filepath = datapath, 
                    lhs = lhs_columns, 
                    rhs = rhs_column,
                    binary_name = config["binary_name"],
                    algo = config["algo"]
                )
            else:
                stats = run_python_metric(
                    metric_func = config["function"], 
                    csv_filepath = datapath,
                    lhs = lhs_columns, 
                    rhs = rhs_column
                )
                
            load_time = round(stats["load_time_s"], 5)
            build_time = round(stats["build_time_s"], 5)
            compute_time = round(stats["compute_time_s"], 5)
            total_time = round(load_time + build_time + compute_time, 5)
            memory_used = round(stats["memory_used_mb"], 5)

            mu = stats.get("mu_plus")
            rfi = stats.get("rfi_prime_plus")

            if "result_value" in stats:
                if "mu_plus" in config["name"]:
                    mu = stats["result_value"]
                elif "rfi" in config["name"]:
                    rfi = stats["result_value"]
                                        
            results.append({
                "scenario": scenario["name"],
                "implementation": config["name"],
                "mu_plus": round(mu, 5) if mu is not None else None,
                "rfi_prime_plus": round(rfi, 5) if rfi is not None else None,
                "load_time_s": round(load_time, 5),
                "build_time_s": build_time,
                "compute_time_s": compute_time,
                "total_time_s": total_time,
                "memory_used_mb": memory_used,
            })
        
    save_results(results)
    
    return results
