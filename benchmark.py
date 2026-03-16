import os
import pandas as pd
import subprocess
import time
import tracemalloc
from typing import Any, Callable, Dict, Tuple

from adapted_paper_metrics import mu_plus, reliable_fraction_of_information_prime_plus
from opt_metrics.mu_plus_opt import mu_plus_opt, cpp_mu_plus_opt
# from opt_metrics.rfi_plus_opt import reliable_fraction_of_information_prime_plus_opt
from synthetic_data.plot import plot_rank_frequency
from synthetic_data.generator import generate_SYN 


def get_dataset_path(scenario: Dict[str, Any]) -> str:
    
    filename = (f"{scenario["name"]}.csv")
    return os.path.join("data", filename)


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
        "n_type": "copy"
    }


def generate_dataset(scenario: Dict[str, Any]) -> None:

    filepath = get_dataset_path(scenario)
    generation_args = get_generation_args(scenario)
        
    df = generate_SYN(**generation_args)
    df.to_csv(filepath, index = False)


def load_dataset(filepath: str) -> pd.DataFrame:

    return pd.read_csv(filepath)


def run_metric(
    metric_func: Callable[..., float], 
    call_args: Dict[str, Any],
    is_cpp: bool = False
) -> Tuple[float, float, float]:
    
    if is_cpp:
        return metric_func(**call_args)
 
    tracemalloc.start()
    memory_before, _ = tracemalloc.get_traced_memory()

    start_time = time.time()
    
    result = metric_func(**call_args)
    
    duration = time.time() - start_time
    _, memory_peak = tracemalloc.get_traced_memory()
    
    memory_used = (memory_peak - memory_before) / (1024**2
                                                   )
    tracemalloc.stop()
    
    return result["result"], duration, memory_used



def run_benchmarks(regenerate: bool = False) -> pd.DataFrame:

    results = []
    output_path = "data/benchmark.csv"
    
    for scenario in scenarios:
        filepath = get_dataset_path(scenario)
        if regenerate or not os.path.exists(filepath):
            generate_dataset(scenario)
            print(f"\nSaved {scenario["name"]} data on {filepath}\n")
        
        load_start = time.time()
        df = pd.read_csv(filepath)
        load_time = time.time() - load_start
        
        for metric_name, metric_func, metric_args in metrics_config:            
            is_cpp = metric_name.startswith("cpp")
        
            print(f"Running: {scenario["name"]}, {metric_name}.\n")
            
            call_args = {"df": df}
            call_args.update(metric_args)
            
            if is_cpp:
                call_args["filepath"] = filepath
                call_args.pop("df")
                            
            metric_value, execution_time, memory_used = run_metric(metric_func, call_args, is_cpp)
            
            results.append({
                "scenario": scenario["name"],
                "implementation": metric_name,
                "result_value": round(metric_value, 5),
                "load_time(s)": round(load_time, 5),
                "execution_time(s)": round(execution_time, 5),
                "total_time(s)": round(load_time + execution_time, 5),
                "memory_used(MB)": round(memory_used, 5),
            })
        
    if results:
        df_results = pd.DataFrame(results)
        df_results.to_csv(output_path, index = False)
        print(f"\nResults saved in {output_path}")
        
        return df_results

    else:
        print("\nNo results")
        
    return pd.DataFrame()
    

scenarios = [
    {
        "name": "zipf_100k", 
        "tuples": 100_000,
        "lhs_sels": [0.5],
        "rhs_sel": 0.5,
        "dist_params": {
            "dist_type": "zipf", 
            "lhs_dist_alpha": 1.01, 
            "lhs_dist_beta": 0, 
            "rhs_dist_alpha": 1.01, 
            "rhs_dist_beta": 0,
            "noise": 0.1
        }
    },
    {
        "name": "zipf_110k", 
        "tuples": 110_000,
        "lhs_sels": [0.5],
        "rhs_sel": 0.5, 
        "dist_params": {
            "dist_type": "zipf", 
            "lhs_dist_alpha": 1.01, 
            "lhs_dist_beta": 0, 
            "rhs_dist_alpha": 1.01, 
            "rhs_dist_beta": 0,
            "noise": 0.1
        }
    },
    {
        "name": "zipf_120k", 
        "tuples": 120_000, 
        "lhs_sels": [0.5],
        "rhs_sel": 0.5, 
        "dist_params": {
            "dist_type": "zipf", 
            "lhs_dist_alpha": 1.01,
            "lhs_dist_beta": 0, 
            "rhs_dist_alpha": 1.01, 
            "rhs_dist_beta": 0,
            "noise": 0.1
        }
    },
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
    #         "noise": 0.1
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
    #         "noise": 0.1
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
    #         "noise": 0.3
    #     }
    # },
]


metrics_config = [
    # (
    #     "py_mu_plus",
    #     mu_plus, 
    #     {
    #         "lhs": ["lhs"], 
    #         "rhs": "rhs"
    #     },
    # ),
    # (
    #     "py_mu_plus_opt",
    #     mu_plus_opt, 
    #     {
    #         "lhs": ["lhs"], 
    #         "rhs": "rhs"
    #     },
    # ),
    (
        "cpp_mu_plus_auto",
        cpp_mu_plus_opt, 
        {
            "lhs": ["lhs"], 
            "rhs": "rhs",
        }, 
    ),
    (
        "cpp_mu_plus_bitmap",
        cpp_mu_plus_opt, 
        {
            "lhs": ["lhs"], 
            "rhs": "rhs",
            "algo": "bitmap"
        }, 
    ),
    (
        "cpp_mu_plus_hash",
        cpp_mu_plus_opt, 
        {
            "lhs": ["lhs"], 
            "rhs": "rhs",
            "algo": "hash"
        }, 
    ),
    # (
    #     "rfi_prime_plus"
    #     reliable_fraction_of_information_prime_plus, 
    #     {
    #         "lhs": ["lhs"], 
    #         "rhs": "rhs"
    #     }, 
    # ),
]
