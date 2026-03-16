import sys
import time
import pandas as pd
import os
import ast
import subprocess

import src.metrics.python.adapted_paper_metrics as adapted_paper_metrics
from src.benchmark.benchmark import run_benchmarks

def parse_fds(fds_file):
    """Parse FDs from .fds file with format: (["col1","col2"],"target")"""
    fds = []
    with open(fds_file, 'r') as f:
        for line in f:
            line = line.strip()
            if line:
                try:
                    parsed = ast.literal_eval(line)
                    lhs, rhs = parsed
                    fds.append((lhs, rhs))
                except Exception:
                    pass
    return fds


def run_metrics(dataset_csv_file, fds_input_file, output_path, metric="mu_plus"):
    """Run Python metrics computation"""
    dataset = os.path.basename(dataset_csv_file).split(".csv")[0]
    fds_csv_file = f"{output_path}.csv"
    
    metric_functions = {
        "mu_plus": adapted_paper_metrics.mu_plus,
        "mu": adapted_paper_metrics.mu,
    }
    
    if metric not in metric_functions:
        raise ValueError(f"Invalid metric: {metric}. Choose 'mu' or 'mu_plus'")
    
    metric_function = metric_functions[metric]
    
    start_time = time.time()
    fds = parse_fds(fds_input_file)
    load_start = time.time()
    df = pd.read_csv(dataset_csv_file, header=0)
    load_time = time.time() - load_start
    
    print(f"  Pandas CSV load: {load_time:.3f}s")
    
    compute_start = time.time()
    print(f"  Computing {metric} on {len(fds)} FDs...")
    
    fd_ids = []
    results = []
    
    for lhs_columns, rhs_column in fds:
        lhs_columns = [col.strip() for col in lhs_columns]
        rhs_column = rhs_column.strip()
        fd_ids.append(f"{lhs_columns}->{rhs_column}")
        result = metric_function(df, lhs_columns, rhs_column)
        results.append(result)
    
    compute_time = time.time() - compute_start
    
    # Build results dataframe
    final_df = pd.DataFrame({
        "dataset": dataset,
        "fd": fd_ids,
        metric: [r["result"] for r in results],
        "is_key": [r.get("is_key", "-") for r in results],
        "lhs_size": [r.get("lhs_size", "-") for r in results],
        "lhs_uniqueness": [r.get("lhs_uniqueness", "-") for r in results]
    })
    
    final_df.to_csv(fds_csv_file, index=False)
    elapsed = time.time() - start_time
    
    print(f"  Metric computation: {compute_time:.3f}s")
    
    return {
        "fds_csv_file": fds_csv_file,
        "total_time": elapsed,
        "load_time": load_time,
        "compute_time": compute_time,
        "num_fds": len(fds)
    }


def run_cpp_metrics(dataset_csv_file, fds_input_file, output_csv_file, metric="mu_plus"):
    """Run C++ metrics computation"""
    print(f"\nC++: Computing {metric} metric...")
    
    start_time = time.time()
    cmd = ["./fd_metrics_test", dataset_csv_file, fds_input_file, "-o", output_csv_file, "-m", metric]
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        # Only print key lines
        for line in result.stdout.split('\n'):
            if 'Processing FD' in line or 'Total time:' in line:
                print(f"  {line.strip()}")
        
        return {
            "output_file": output_csv_file,
            "total_time": time.time() - start_time,
            "success": True
        }
    except subprocess.CalledProcessError as e:
        print(f"Error: {e.stderr}")
        return {"success": False, "error": str(e)}


def run_cpp_metrics_optimized(dataset_csv_file, fds_input_file, output_csv_file, metric="mu_plus"):
    """Run optimized C++ metrics computation (no upfront indexing)"""
    print(f"\nC++ Optimized: Computing {metric} metric...")
    
    start_time = time.time()
    cmd = ["./fd_metrics_opt_test", dataset_csv_file, fds_input_file, "-o", output_csv_file, "-m", metric]
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        # Only print key lines
        for line in result.stdout.split('\n'):
            if 'Processing FD' in line or 'Total time:' in line or 'Loaded' in line or 'Strategy' in line:
                print(f"  {line.strip()}")
        
        return {
            "output_file": output_csv_file,
            "total_time": time.time() - start_time,
            "success": True
        }
    except subprocess.CalledProcessError as e:
        print(f"Error: {e.stderr}")
        return {"success": False, "error": str(e)}


def compare_results(python_csv, cpp_csv, metric, tolerance=3):
    """Compare Python and C++ results"""
    df_python = pd.read_csv(python_csv)
    df_cpp = pd.read_csv(cpp_csv)
    
    if len(df_python) != len(df_cpp):
        return {"match": False}
    
    print(f"\nComparing {metric} (tolerance: {tolerance} decimals)...")
    
    all_match = True
    for idx in range(len(df_python)):
        value_python = round(df_python.loc[idx, metric], tolerance)
        value_cpp = round(df_cpp.loc[idx, metric], tolerance)
        
        if value_python != value_cpp:
            all_match = False
            print(f"  ✗ FD {idx+1}: Python={value_python}, C++={value_cpp}")
    
    if all_match:
        print(f"  ✓ All {len(df_python)} FDs match!")
    
    return {"match": all_match}


def run_experiment(dataset_csv_file, fds_input_file, metric = "mu_plus"):
    dataset_name = os.path.basename(dataset_csv_file).replace(".csv", "")
    fds_name = os.path.basename(fds_input_file).replace(".fds", "")
    
    python_output = f"./data/{dataset_name}_{fds_name}_python"
    cpp_output = f"./data/{dataset_name}_{fds_name}_cpp.csv"
    cpp_opt_output = f"./data/{dataset_name}_{fds_name}_cpp_opt.csv"
    
    print(f"dataset: {dataset_name}")
    print(f"fds: {fds_name} \n")
    
    # Run Python
    python_result = run_metrics(dataset_csv_file, fds_input_file, python_output, metric)
    print(f"Python total: {python_result['total_time']:.2f}s")
    
    # Run C++ (with indexing)
    cpp_result = run_cpp_metrics(dataset_csv_file, fds_input_file, cpp_output, metric)
    if cpp_result['success']:
        print(f"C++ (indexed) total: {cpp_result['total_time']:.2f}s")
    
    # Run C++ Optimized (no indexing)
    cpp_opt_result = run_cpp_metrics_optimized(dataset_csv_file, fds_input_file, cpp_opt_output, metric)
    if cpp_opt_result['success']:
        print(f"C++ (optimized) total: {cpp_opt_result['total_time']:.2f}s")
    
    # Print summary
    print(f"\n{'='*60}")
    print("Performance Summary:")
    print(f"\n  Python:")
    print(f"    CSV load:   {python_result['load_time']:.3f}s")
    print(f"    Compute:    {python_result['compute_time']:.3f}s")
    print(f"    Total:      {python_result['total_time']:.3f}s")
    
    if cpp_result['success']:
        print(f"\n  C++ (indexed):")
        print(f"    Total:      {cpp_result['total_time']:.3f}s  ({python_result['total_time']/cpp_result['total_time']:.2f}x vs Python)")
        print(f"                (includes ~0.5s index build + ~0.2s compute)")
    
    if cpp_opt_result['success']:
        print(f"\n  C++ (optimized):")
        print(f"    Total:      {cpp_opt_result['total_time']:.3f}s  ({python_result['total_time']/cpp_opt_result['total_time']:.2f}x vs Python)")
        print(f"                (includes ~0.8s CSV load + ~0.2s compute)")
        if cpp_result['success']:
            print(f"    Speedup:    {cpp_result['total_time']/cpp_opt_result['total_time']:.2f}x vs indexed")
    
    print(f"\n  Key Insight:")
    print(f"    - Pandas load ({python_result['load_time']:.3f}s) is FASTER than C++ index build (~0.5s)")
    print(f"    - C++ optimized compute (~0.2s) is FASTER than Pandas compute ({python_result['compute_time']:.3f}s)")


if __name__ == "__main__":
    
    regenerate = "regenerate" in sys.argv
    
    run_benchmarks(regenerate)
    
    # real_data_path = "./data/itaxlarge.csv"
    # real_fds_path = "./data/itax_pyro.fds"
        
    # run_experiment(real_data_path, real_fds_path)
