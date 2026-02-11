import os
import sys
import tracemalloc
import pandas as pd
import time
from typing import Any, Callable, Dict, List, Tuple

from synthetic_data.generator import generate_SYN 
from adapted_paper_metrics import mu_plus, reliable_fraction_of_information_prime_plus
from opt_metrics.mu_plus_opt import mu_plus_opt
# from opt_metrics.rfi_plus_opt import reliable_fraction_of_information_prime_plus_opt


def get_dataset_path(scenario: Dict[str, Any]) -> str:
    
    filename = (f"{scenario["name"]}.csv")
    return os.path.join("data", filename)


def get_generation_args(scenario: Dict[str, Any]) -> Dict[str, Any]:
    
    n_rows = scenario["tuples"]
    dist = scenario["dist_params"]
    
    return {
        "fd": True,
        "tuples": scenario["tuples"],
        "lhs_cardinality": int(n_rows * 0.6),
        "rhs_cardinality": int(n_rows * 0.001),
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
    call_args: Dict[str, Any]
) -> Tuple[float, float, float]:
 
    tracemalloc.start()
    start_time = time.time()
    
    result = metric_func(**call_args)
    
    duration = time.time() - start_time
    _, peak_mem = tracemalloc.get_traced_memory()
    tracemalloc.stop()
    
    return result["result"], duration, (peak_mem / (1024**2))


def run_benchmark(
    metric_func: Callable[..., float], 
    scenarios: List[Dict[str, Any]], 
    metric_args: Dict[str, Any]
    ) -> pd.DataFrame:
    
    results = []

    for scenario in scenarios:
        
        filepath = get_dataset_path(scenario)
        
        start_load = time.time()
        df = load_dataset(filepath)
        time_load = time.time() - start_load
        
        call_args = {'df': df}
        call_args.update(metric_args)
        
        metric_value, time_execution, memory_peak = run_metric(metric_func, call_args)
        
        results.append({
            "scenario": scenario["name"],
            "tuples": scenario["tuples"],
            "metric_value": metric_value,
            "load_time(s)": round(time_load, 5),
            "exec_time(s)": round(time_execution, 5),
            "total_time(s)": round(time_load + time_execution, 5),
            "memory_peak(MB)": memory_peak
        })

    return pd.DataFrame(results)


scenarios = [
    {
        "name": "zipf_100k", 
        "tuples": 100_000, 
        "dist_params": {
            "dist_type": "zipf", 
            "lhs_dist_alpha": 1.01, 
            "lhs_dist_beta": 0, 
            "rhs_dist_alpha": 1.01, 
            "rhs_dist_beta": 0
        }
    },
    {
        "name": "zipf_1m", 
        "tuples": 1_000_000, 
        "dist_params": {
            "dist_type": "zipf", 
            "lhs_dist_alpha": 1.01, 
            "lhs_dist_beta": 0, 
            "rhs_dist_alpha": 1.01, 
            "rhs_dist_beta": 0
        }
    },
    {
        "name": "zipf_10m", 
        "tuples": 10_000_000, 
        "dist_params": {
            "dist_type": "zipf", 
            "lhs_dist_alpha": 1.01,
            "lhs_dist_beta": 0, 
            "rhs_dist_alpha": 1.01, 
            "rhs_dist_beta": 0
        }
    },
    # {
    #     "name": "zipf_100m", 
    #     "tuples": 100_000_000, 
    #     "dist_params": {
    #         "dist_type": "zipf", 
    #         "lhs_dist_alpha": 2, 
    #         "lhs_dist_beta": 0, 
    #         "rhs_dist_alpha": 2, 
    #         "rhs_dist_beta": 0
    #     }
    # },
    {
        "name": "beta_1m", 
        "tuples": 1_000_000, 
        "dist_params": {
            "dist_type": "beta",
            "lhs_dist_alpha": 2.0,
            "lhs_dist_beta": 5.0, 
            "rhs_dist_alpha": 2.0,
            "rhs_dist_beta": 5.0,
        }
    },
]


metrics_config = [
    (
        mu_plus, 
        {
            "lhs": ["lhs"], 
            "rhs": "rhs"
        },
        "data/mu_plus.csv"
    ),
    # (
    #     reliable_fraction_of_information_prime_plus, 
    #     {
    #         "lhs": ["lhs"], 
    #         "rhs": "rhs"
    #     }, 
    #     "data/rfi_plus.csv"
    # ),
]


def run_benchmark(regenerate: bool):
    
    regenerate = "regenerate" in sys.argv
    
    for scenario in scenarios:
        filepath = get_dataset_path(scenario)
        if regenerate or not os.path.exists(filepath):
            generate_dataset(scenario)
            print(f"Saved {scenario["name"]} data on {filepath}")
            
    for func, args, output_path in metrics_config:
        df_res = run_benchmark(func, scenarios, args)
        
        df_res.to_csv(output_path, index = False)
        print(f"Saved {func.__name__} results in {output_path}\n")
    
    # df_rfi_plus = run_benchmark(
    #     metric_func = reliable_fraction_of_information_prime_plus,
    #     scenarios = scenarios,
    #     metric_args = {"lhs": ["lhs"], "rhs": "rhs"}
    # )
    
    # output_rfi_plus = "data/rfi_plus.csv"
    # df_rfi_plus.to_csv(output_rfi_plus, index = False)
    
    
