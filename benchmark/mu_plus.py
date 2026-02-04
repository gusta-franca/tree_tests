import time
import pandas as pd
from typing import List, Any
from synthetic_data.generator import generate_SYN
from adapted_paper_metrics import pdep_self, pdep, mu_plus


def pdep_self_opt(df: pd.DataFrame, y: Any) -> float:
    
    return (df.groupby(y, sort = False).size() / df.shape[0]).pow(2).sum()
    
    
def pdep_opt(df: pd.DataFrame, lhs: List[Any], rhs: Any) -> float:
    
    combined_columns = lhs + [rhs]
    
    xy_counts = df.groupby(combined_columns, sort = False).size().reset_index(name="xy_count")
    
    x_counts = xy_counts.groupby(lhs, sort = False)["xy_count"].sum().reset_index(name="x_count")
    
    counts = xy_counts.merge(x_counts, on=lhs)

    return (1 / df.shape[0]) * (counts["xy_count"].pow(2) / counts["x_count"]).sum()


def mu_plus_opt(df: pd.DataFrame, lhs: List[Any], rhs: Any) -> float:
    
    pdepXY = pdep_opt(df, lhs, rhs)  # Usa a função pdep adaptada
    pdepY = pdep_self_opt(df, rhs)  # Usa a função pdep_self que não precisa ser adaptada
    r_size = df.shape[0]

    domX_size = df.loc[:, lhs].drop_duplicates().shape[0]
    lhs_uniqueness = domX_size / r_size

    # if the fd's LHS is a key, rsize=domx_size, therefore there will be a div / 0
    if r_size == domX_size:
        return {"result": 1.0, "is_key": True}

    return {
        "result": max(
            1.0 - ((1 - pdepXY) / (1 - pdepY)) * ((r_size - 1) / (r_size - domX_size)),
            0,
        ),
        "is_key": False,
        "lhs_uniqueness": lhs_uniqueness,
        "lhs_size": len(lhs),
    }

def run_benchmark():
    scenarios = [
        {
            "name": "zipf_small", "tuples": 100_000, 
            "dist_params": {"dist_type": "zipf", "lhs_dist_alpha": 1.01, "lhs_dist_beta": 0, "rhs_dist_alpha": 2, "rhs_dist_beta": 0}
        },
        {
            "name": "zipf_medium", "tuples": 1_000_000, 
            "dist_params": {"dist_type": "zipf", "lhs_dist_alpha": 1.01, "lhs_dist_beta": 0, "rhs_dist_alpha": 2, "rhs_dist_beta": 0}
        },
        {
            "name": "zipf_big", "tuples": 10_000_000, 
            "dist_params": {"dist_type": "zipf", "lhs_dist_alpha": 1.01, "lhs_dist_beta": 0, "rhs_dist_alpha": 2, "rhs_dist_beta": 0}
        },
        # {
        #     "name": "zipf_biggest", "tuples": 100_000_000, 
        #     "dist_params": {"dist_type": "zipf", "lhs_dist_alpha": 2, "lhs_dist_beta": 0, "rhs_dist_alpha": 2, "rhs_dist_beta": 0}
        # },
        {
            "name": "beta", "tuples": 1_000_000, 
            "dist_params": {"dist_type": "beta", "lhs_dist_alpha": 2.0, "lhs_dist_beta": 5.0, "rhs_dist_alpha": 2.0, "rhs_dist_beta": 5.0}
        },
    ]
    
    params = {
        "fd": True,
        "lhs_cardinality": 0.1,
        "rhs_cardinality": 0.01,
        "noise": 0.05,
        "n_type": "copy"
    }
    
    results = []
    
    print(f"{'scenario':<15} | {'rows':<10} | {'time_og (s)':<11} | {'time_opt (s)':<12} | {'speedup':<7}")
    print("-" * 75)
    
    for scenario in scenarios:
        n_rows = scenario["tuples"]
        
        current_params = params.copy()
        current_params.update(scenario["dist_params"])
        current_params["tuples"] = n_rows
        current_params["lhs_cardinality"] = int(n_rows*0.6) 
        current_params["rhs_cardinality"] = int(n_rows*0.001)
        # current_params["n_type"] = "bogus"
       
        df = generate_SYN(**current_params)
        
        df.columns = ["lhs", "rhs"]
        lhs_input = ["lhs"]
        rhs_input = "rhs"

        start_og = time.perf_counter()
        result_og = mu_plus(df, lhs_input, rhs_input)
        time_og = time.perf_counter() - start_og
        
        start_opt = time.perf_counter()
        result_opt = mu_plus_opt(df, lhs_input, rhs_input)
        time_opt = time.perf_counter() - start_opt
        
        # print(result_og["result"])
        # print(result_opt["result"])
        # print()

        speedup = time_og / time_opt
        
        print(f"{scenario['name']:<15} | {n_rows:<10} | {time_og:.4f} | {time_opt:.4f} | {speedup:.2f}x")
                
        results.append({
            "scenario": scenario["name"],
            "rows": n_rows,
            "distribution": current_params["dist_type"],
            "time_og": round(time_og, 9),
            "time_opt": round(time_opt, 9),
            "speedup": round(speedup, 2),
            "metric_value": result_opt['result']
        })

    results_df = pd.DataFrame(results)
    output_file = "benchmark/mu_plus.csv"
    results_df.to_csv(output_file, index = False)
    
    print(f"\nSaved in '{output_file}'")
