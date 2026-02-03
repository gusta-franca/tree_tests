import time
import pandas as pd
import os
import ast
from datetime import datetime
import adapted_paper_metrics


def parse_fds(fds_file):
    """
    Parse FDs from .fds file with format: (["col1","col2"],"target")
    Returns list of tuples: [(["col1","col2"], "target"), ...]
    """
    fds = []
    with open(fds_file, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            # Parse the tuple format (["col1","col2"],"target")
            try:
                parsed = ast.literal_eval(line)
                lhs, rhs = parsed
                fds.append((lhs, rhs))
            except Exception as e:
                print(f"Error parsing line: {line}, error: {e}")
                continue
    return fds


def run_metrics(dataset_csv_file, fds_input_file, output_path=None):
    """
    Run metrics on datasets

    Args:
        dataset_csv_file (str): Path to the dataset CSV file
        fds_input_file (str): Path to the FDs input file
        output_path (str, optional): Full path for output file (without extension)

    Returns:
        dict: Results containing output paths and metrics
    """
    dataset = os.path.basename(dataset_csv_file).split(".csv")[0]

    if output_path:
        # Use the provided output path
        output_file_base = output_path
        output_folder = os.path.dirname(output_path)
        if output_folder:
            os.makedirs(output_folder, exist_ok=True)
    else:
        # Default behavior
        dir_name = datetime.now().strftime("%d-%m-%Y-%H-%M-%S-") + dataset
        output_folder = os.path.join("metrics_results", dir_name)
        os.makedirs(output_folder, exist_ok=True)
        output_file_base = os.path.join(output_folder, f"{dataset}-metrics")

    fds_csv_file = f"{output_file_base}.csv"
    runtime_csv_file = f"{output_file_base}_runtime.csv"

    # Define metrics to compute
    metrics = {
        "mu_plus": adapted_paper_metrics.mu_plus,
        # "rfi_prime_plus": adapted_paper_metrics.reliable_fraction_of_information_prime_plus,
        "g3_prime": adapted_paper_metrics.g3_prime,
    }

    start_time_file = time.time()
    total_time_metrics = {metric_name: 0 for metric_name in metrics}

    # Parse FDs from file
    fds = parse_fds(fds_input_file)
    print(f"Loaded {len(fds)} FDs from {fds_input_file}")

    # Load dataset
    df = pd.read_csv(dataset_csv_file, header=0)
    num_columns = df.shape[1]
    print(
        f"Loaded dataset {dataset_csv_file} with {num_columns} columns and {len(df)} rows")

    fd_ids = []
    metrics_results = {key: [] for key in metrics}

    # Process FDs one by one (sequential execution)
    for idx, (lhs_columns, rhs_column) in enumerate(fds, 1):
        lhs_columns = [col.strip() for col in lhs_columns]
        rhs_column = rhs_column.strip()
        fd_identifier = f"{lhs_columns}->{rhs_column}"

        print(f"Processing FD {idx}/{len(fds)}: {fd_identifier}")

        if not lhs_columns:
            print(f"  Skipping empty LHS")
            continue

        fd_ids.append(fd_identifier)

        if not set(lhs_columns).issubset(df.columns) or rhs_column not in df.columns:
            print(f"  Columns not found in dataset")
            continue

        # Compute each metric sequentially for this FD
        for metric_name, metric_function in metrics.items():
            start_time = time.time()
            try:
                result = metric_function(df, lhs_columns, rhs_column)
                metrics_results[metric_name].append(result)
                elapsed_time = time.time() - start_time
                total_time_metrics[metric_name] += elapsed_time
                print(
                    f"  {metric_name}: {result['result']:.4f} ({elapsed_time:.2f}s)")
            except Exception as e:
                print(f"  [ERROR] {metric_name}: {e}")
                continue

    # Extract results
    mobj = {k: [v["result"] for v in values]
        for k, values in metrics_results.items()}

    # Extract optional metadata
    isKey = {}
    lhs_size = {}
    lhs_uniqueness = {}

    for k, values in metrics_results.items():
        if any("is_key" in v for v in values):
            isKey["is_key"] = [v.get("is_key", "-") for v in values]
        if any("lhs_size" in v for v in values):
            lhs_size["lhs_size"] = [v.get("lhs_size", "-") for v in values]
        if any("lhs_uniqueness" in v for v in values):
            lhs_uniqueness["lhs_uniqueness"] = [
                v.get("lhs_uniqueness", "-") for v in values]

    # Create results dataframe
    final_df = pd.DataFrame({
        "dataset": dataset,
        "fd": fd_ids,
        **mobj,
        **isKey,
        **lhs_size,
        **lhs_uniqueness
    })

    final_df.to_csv(fds_csv_file, index=False)
    print(f"Results saved to {fds_csv_file}")

    # Save runtime metrics
    total_time_file = time.time() - start_time_file
    runtime_data = []
    for metric_name, total_time in total_time_metrics.items():
        runtime_data.append({
            "file": dataset_csv_file,
            "metric": metric_name,
            "metric_time": total_time,
            "num_columns": num_columns,
            "num_fds": len(fds),
        })

    runtime_data.append({
        "file": dataset_csv_file,
        "metric": "total_file_time",
        "metric_time": total_time_file,
        "num_columns": num_columns,
        "num_fds": len(fds),
    })

    pd.DataFrame(runtime_data).to_csv(runtime_csv_file, index=False)
    print(f"Runtime metrics saved to {runtime_csv_file}")

    return {
        "fds_csv_file": fds_csv_file,
        "runtime_csv_file": runtime_csv_file,
        "output_folder": output_folder if not output_path else os.path.dirname(fds_csv_file),
        "total_time": total_time_file,
        "num_fds": len(fds)
    }


if __name__ == "__main__":
    # Configure paths here
    dataset_csv_file = "./data/itax_small.csv"
    # dataset_csv_file = "./data/itaxlarge.csv"
    fds_input_file = "./data/itax.fds"
    output_path = "./data/itax.fds_metrics_python"

    # Run metrics
    result = run_metrics(
        dataset_csv_file=dataset_csv_file,
        fds_input_file=fds_input_file,
        output_path=output_path
    )

    print(f"\n=== Summary ===")
    print(f"Total FDs processed: {result['num_fds']}")
    print(f"Total time: {result['total_time']:.2f} seconds")
    print(f"Results saved to: {result['fds_csv_file']}")
    print(f"Runtime saved to: {result['runtime_csv_file']}")


    

