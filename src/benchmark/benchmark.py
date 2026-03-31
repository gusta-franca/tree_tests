from datetime import datetime
import os
import pandas as pd
import subprocess
import time
import tracemalloc
from typing import Any, Callable, Dict, List, Tuple

from src.metrics.python.adapted_paper_metrics import mu_plus, reliable_fraction_of_information_prime_plus
from src.metrics.python.mu_plus_opt import mu_plus_opt, cpp_mu_plus_opt
# from src.metrics.python.rfi_plus_opt import reliable_fraction_of_information_prime_plus_opt
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
        "fd": True,
        "tuples": scenario["tuples"],
        "lhs_sels": scenario.get("lhs_sels"),
        "rhs_sel": scenario.get("rhs_sel"),
        "lhs_dist_alpha": dist["lhs_dist_alpha"],
        "lhs_dist_beta": dist["lhs_dist_beta"],
        "rhs_dist_alpha": dist["rhs_dist_alpha"],
        "rhs_dist_beta": dist["rhs_dist_beta"],
        "noise": dist.get("noise", 0.01),
        "dist_type": dist["dist_type"],
        "n_type": dist["n_type"],
    }


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
    

def run_python_metric(metric_func, df, lhs, rhs):
    
    tracemalloc.start()
    start_time = time.time()
    
    result = metric_func(df = df, lhs = lhs, rhs = rhs)
    
    compute_time = time.time() - start_time
    _, memory_peak = tracemalloc.get_traced_memory()
    tracemalloc.stop()
    
    return {
        "result_value": result["result"],
        "build_time_s": 0.0,  # Pandas builds and computes at the same time 
        "compute_time_s": compute_time,
        "memory_used_mb": memory_peak / (1024 * 1024)
    }


def run_benchmarks(regenerate: bool = False) -> pd.DataFrame:
    
    metrics_config = [
        {"name": "py_mu_plus", "function": mu_plus, "is_cpp": False},
        {"name": "py_mu_plus_opt", "function": mu_plus_opt, "is_cpp": False},
        {"name": "cpp_mu_plus_auto", "function": cpp_mu_plus_opt, "is_cpp": True, "binary_name": "fd_metrics_opt_test"},
        {"name": "cpp_mu_plus_partitioned", "function": cpp_mu_plus_opt, "is_cpp": True, "binary_name": "fd_metrics_partitioned_test"},
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
        # {
        #     "name": "rfi_prime_plus",
        #     "function": reliable_fraction_of_information_prime_plus,
        #     "language": "python",
        #     "lhs": lhs_columns, 
        #     "rhs": "rhs",
        # },
    ]
    
    results = []
    
    for scenario in scenarios:        
        datapath = get_dataset_path(scenario)
        if regenerate or not os.path.exists(datapath):
            generate_dataset(scenario)
            print(f"\nSaved {scenario["name"]} data on {datapath}\n")
        
        load_start = time.time()
        df = pd.read_csv(datapath)
        load_time = time.time() - load_start
        # plot_rank_frequency(df)
        
        lhs_columns = [f"lhs_{i}" for i in range(len(scenario["lhs_sels"]))]
        rhs_column = "rhs"
        
        for config in metrics_config:            
            print(f"Running: {scenario["name"]}, {config["name"]}.\n")
            
            if config["is_cpp"]:
                stats = config["function"](
                    filepath = datapath, 
                    lhs = lhs_columns, 
                    rhs = rhs_column,
                    binary_name = config["binary_name"]
                )
            else:
                stats = run_python_metric(
                    metric_func = config["function"], 
                    df = df, 
                    lhs = lhs_columns, 
                    rhs = rhs_column
                )
                
            result_value = round(stats["result_value"], 5)
            build_time = round(stats.get("build_time_s", 0.0), 5)
            compute_time = round(stats["compute_time_s"], 5)
            total_time = round(load_time + build_time + compute_time, 5)
            memory_used = round(stats["memory_used_mb"], 5)
                                        
            results.append({
                "scenario": scenario["name"],
                "implementation": config["name"],
                "result_value": result_value,
                "load_time_s": round(load_time, 5),
                "build_time_s": build_time,
                "compute_time_s": compute_time,
                "total_time_s": total_time,
                "memory_used_mb": memory_used,
            })
        
    save_results(results)
    
    return results


scenarios = [
    {
        "name": "zipf_100k", 
        "tuples": 100000,
        "lhs_sels": [0.01, 0.01, 0.01],
        "rhs_sel": 0.01,
        "dist_params": {
            "dist_type": "zipf", 
            "lhs_dist_alpha": 1.01, 
            "lhs_dist_beta": 0, 
            "rhs_dist_alpha": 1.01, 
            "rhs_dist_beta": 0,
            "noise": 0.01,
            "n_type": "copy",
        }
    },
    # {
    #     "name": "zipf_100k", 
    #     "tuples": 100_000,
    #     "lhs_sels": [0.1, 0.5, 0.01],
    #     "rhs_sel": 0.5, 
    #     "dist_params": {
    #         "dist_type": "zipf", 
    #         "lhs_dist_alpha": 1.01, 
    #         "lhs_dist_beta": 0, 
    #         "rhs_dist_alpha": 1.01, 
    #         "rhs_dist_beta": 0,
    #         "noise": 0.0,
    #         "n_type": "bogus",
    #     }
    # },
    # {
    #     "name": "zipf_120k", 
    #     "tuples": 120_000, 
    #     "lhs_sels": [0.5],
    #     "rhs_sel": 0.5, 
    #     "dist_params": {
    #         "dist_type": "zipf", 
    #         "lhs_dist_alpha": 1.01,
    #         "lhs_dist_beta": 0, 
    #         "rhs_dist_alpha": 1.01, 
    #         "rhs_dist_beta": 0,
    #         "noise": 0.1,
    #         "n_type": "copy",
    #     }
    # },
    # {
    #     "name": "zipf_1m", 
    #     "tuples": 1_000_000, 
    #     "lhs_sels": [0.5],
    #     "rhs_sel": 0.5, 
    #     "dist_params": {
    #         "dist_type": "zipf", 
    #         "lhs_dist_alpha": 1.01, 
    #         "lhs_dist_beta": 0, 
    #         "rhs_dist_alpha": 1.01, 
    #         "rhs_dist_beta": 0,
    #         "noise": 0.1,
              #"n_type": "bogus",
    #     }
    # },
    # {
    #     "name": "zipf_100m", 
    #     "tuples": 100_000_000, 
    #     "lhs_sels": [0.5],
    #     "rhs_sel": 0.5, 
    #     "dist_params": {
    #         "dist_type": "zipf", 
    #         "lhs_dist_alpha": 1.01, 
    #         "lhs_dist_beta": 0, 
    #         "rhs_dist_alpha": 1.01, 
    #         "rhs_dist_beta": 0,
    #         "noise": 0.1,
             #"n_type": "bogus",
    #     }
    # },
    # {
    #     "name": "beta_1m", 
    #     "tuples": 1_000_000, 
    #     "lhs_sels": [0.5],
    #     "rhs_sel": 0.5, 
    #     "dist_params": {
    #         "dist_type": "beta",
    #         "lhs_dist_alpha": 2.0,
    #         "lhs_dist_beta": 5.0, 
    #         "rhs_dist_alpha": 2.0,
    #         "rhs_dist_beta": 5.0,
    #         "noise": 0.3,
             #"n_type": "bogus",
    #     }
    # },
]
