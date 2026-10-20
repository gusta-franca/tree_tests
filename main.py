import sys

from src.python.benchmark.benchmark import prepare_datasets, run_benchmarks, run_benchmark_fd_ground_truth


def main():
    regenerate = "regenerate" in sys.argv

    if "fd" in sys.argv:
        data_type = "dirty_data" if "dirty" in sys.argv else "clean_data"
        run_benchmark_fd_ground_truth(data_type = data_type)

    else:
        prepare_datasets(scenarios, regenerate)

        run_benchmarks(scenarios)   
