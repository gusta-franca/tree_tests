import os
import pandas as pd
from typing import Any, Dict

from src.python.benchmark.plot import plot_rank_frequency

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

def violation_rate(row_count: int, violation_rows: list, threshold: float = 0.05):
    return len(violation_rows) / row_count > threshold

def numeric_type(data: pd.DataFrame, left_col: str, right_col: str) -> bool:
    return (pd.api.types.is_numeric_dtype(data[left_col]) and pd.api.types.is_numeric_dtype(data[right_col]))
