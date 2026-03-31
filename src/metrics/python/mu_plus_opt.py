import pandas as pd
import subprocess
from typing import Any, Dict, List

    
def pdep_opt(df: pd.DataFrame, lhs: List[Any], rhs: Any) -> float:
    
    combined_columns = lhs + [rhs]
    
    xy_counts = df.groupby(combined_columns, sort = False).size().reset_index(name = "xy_count")
    
    x_counts = xy_counts.groupby(lhs, sort = False)["xy_count"].sum().reset_index(name = "x_count")
    
    counts = xy_counts.merge(x_counts, on = lhs)

    return (1 / df.shape[0]) * (counts["xy_count"].pow(2) / counts["x_count"]).sum()


def pdep_self_opt(df: pd.DataFrame, y: Any) -> float:
    
    return (df.groupby(y, sort = False).size() / df.shape[0]).pow(2).sum()


def mu_plus_opt(df: pd.DataFrame, lhs: List[Any], rhs: Any) -> Dict[str, Any]:
    
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
    

def cpp_mu_plus_opt(filepath: str, lhs: list[str], rhs: str,  binary_name: str, algo: str = "auto"):
    
    binary_path = f"build/bin/{binary_name}" 
    
    lhs_str = ",".join(lhs)
    
    cmd = [binary_path, filepath, lhs_str, rhs, algo]
    
    result = subprocess.run(cmd, capture_output = True, text = True)

    output = result.stdout.strip().split('\n')[-1] # Get last printed line
    parts = output.split(',')
    
    return {
        "result_value": float(parts[0]),
        "build_time_s": float(parts[1]),
        "compute_time_s": float(parts[2]),
        "memory_used_mb": float(parts[3])
    }
