import time
import pandas as pd
import os
import ast
import subprocess
import adapted_paper_metrics


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
                except Exception as e:
                    print(f"Error parsing line: {line}")
    return fds


def run_metrics(dataset_csv_file, fds_input_file, output_path):
    """Run Python metrics computation"""
    dataset = os.path.basename(dataset_csv_file).split(".csv")[0]
    fds_csv_file = f"{output_path}.csv"
    
    metrics = {
        "mu_plus": adapted_paper_metrics.mu_plus,
        "mu": adapted_paper_metrics.mu,
    }
    
    start_time = time.time()
    fds = parse_fds(fds_input_file)
    df = pd.read_csv(dataset_csv_file, header=0)
    
    print(f"Python: Processing {len(fds)} FDs on {len(df)} rows...")
    
    fd_ids = []
    metrics_results = {key: [] for key in metrics}
    
    for lhs_columns, rhs_column in fds:
        lhs_columns = [col.strip() for col in lhs_columns]
        rhs_column = rhs_column.strip()
        fd_ids.append(f"{lhs_columns}->{rhs_column}")
        
        for metric_name, metric_function in metrics.items():
            result = metric_function(df, lhs_columns, rhs_column)
            metrics_results[metric_name].append(result)
    
    # Build results dataframe
    mobj = {k: [v["result"] for v in values] for k, values in metrics_results.items()}
    isKey = {"is_key": [v.get("is_key", "-") for v in metrics_results["mu_plus"]]}
    lhs_size = {"lhs_size": [v.get("lhs_size", "-") for v in metrics_results["mu_plus"]]}
    lhs_uniqueness = {"lhs_uniqueness": [v.get("lhs_uniqueness", "-") for v in metrics_results["mu_plus"]]}
    
    final_df = pd.DataFrame({
        "dataset": dataset,
        "fd": fd_ids,
        **mobj,
        **isKey,
        **lhs_size,
        **lhs_uniqueness
    })
    
    final_df.to_csv(fds_csv_file, index=False)
    elapsed = time.time() - start_time
    
    return {
        "fds_csv_file": fds_csv_file,
        "total_time": elapsed,
        "num_fds": len(fds)
    }
def run_cpp_metrics(dataset_csv_file, fds_input_file, output_csv_file, metric="mu_plus"):
    """Run C++ metrics computation"""
    print(f"\nC++: Computing {metric} metric...")
    
    start_time = time.time()
    cmd = ["./fd_metrics_test", dataset_csv_file, fds_input_file, "-o", output_csv_file, "-m", metric]
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        # Only print key lines from C++ output
        for line in result.stdout.split('\n'):
            if 'Processing FD' in line or 'Total time:' in line or 'Results saved' in line:
                print(f"  {line.strip()}")
        
        return {
            "output_file": output_csv_file,
            "total_time": time.time() - start_time,
            "success": True
        }
    except subprocess.CalledProcessError as e:
        print(f"Error: {e.stderr}")
        return {"success": False, "error": str(e)}


def compare_results(python_csv, cpp_csv, tolerance=3):
    """Compare Python and C++ results"""
    df_python = pd.read_csv(python_csv)
    df_cpp = pd.read_csv(cpp_csv)
    
    if len(df_python) != len(df_cpp):
        return {"match": False}
    
    # Detect metric
    cpp_metric = 'mu_plus' if 'mu_plus' in df_cpp.columns and 'mu' not in df_cpp.columns else 'mu'
    
    print(f"\nComparing {cpp_metric} values (tolerance: {tolerance} decimals)...")
    
    all_match = True
    for idx in range(len(df_python)):
        value_python = round(df_python.loc[idx, cpp_metric], tolerance)
        value_cpp = round(df_cpp.loc[idx, cpp_metric], tolerance)
        
        if value_python != value_cpp:
            all_match = False
            print(f"  ✗ FD {idx+1}: Python={value_python}, C++={value_cpp}")
    
    if all_match:
        print(f"  ✓ All {len(df_python)} FDs match!")
    
    return {"match": all_match}


if __name__ == "__main__":
    print(f"{'='*60}")
    python_result = run_metrics(
        dataset_csv_file=dataset_csv_file,
        fds_input_file=fds_input_file,
        output_path=python_output_base
    )

    print(f"\n=== Python Summary ===")
    print(f"Total FDs processed: {python_result['num_fds']}")
    print(f"Total time: {python_result['total_time']:.2f} seconds")
    print(f"Results saved to: {python_result['fds_csv_file']}")
    
    # Run C++ metrics
    cpp_result = run_cpp_metrics(
        dataset_csv_file=dataset_csv_file,
        fds_input_file=fds_input_file,
        output_csv_file=cpp_output_csv
    )
    
    if cpp_result['success']:
        print(f"\n=== C++ Summary ===")
        print(f"Total time: {cpp_result['total_time']:.2f} seconds")
        print(f"Results saved to: {cpp_result['output_file']}")
        
        # Compare results
        comparison = compare_results(
            python_csv=python_result['fds_csv_file'],
            cpp_csv=cpp_result['output_file'],
            tolerance=3
        )
        
        # Final summary
        print(f"\n{'#'*60}")
        print("FINAL SUMMARY")
        print(f"{'#'*60}")
        print(f"Python time: {python_result['total_time']:.2f}s")
        print(f"C++ time:    {cpp_result['total_time']:.2f}s")
        print(f"Speedup:     {python_result['total_time'] / cpp_result['total_time']:.2f}x")
        print(f"Results match: {'YES ✓' if comparison['match'] else 'NO ✗'}")
        print(f"{'#'*60}\n")
    else:
        print(f"\n✗ C++ execution failed: {cpp_result.get('error', 'Unknown error')}")


