import sys

from src.benchmark.benchmark import prepare_datasets, run_benchmarks


def main():
    regenerate = "regenerate" in sys.argv
    
    prepare_datasets(scenarios, regenerate)

    run_benchmarks(scenarios)   


scenarios = [
    {
        "name": "zipf_100k", 
        "tuples": 100_000,
        "tuple_sel": 0.1,
        "lhs_number": 3,
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
    {
        "name": "zipf_1m", 
        "tuples": 1_000_000,
        "tuple_sel": 0.2,
        "lhs_number": 7,
        "rhs_sel": 0.5, 
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
    {
        "name": "zipf_2m", 
        "tuples": 2_000_000,
        "tuple_sel": 0.1,
        "lhs_number": 3,
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
    {
        "name": "zipf_10m", 
        "tuples": 10_000_000,
        "tuple_sel": 0.1,
        "lhs_number": 7,
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
    # Chashes
    # {
    #     "name": "zipf_100m", 
    #     "tuples": 100_000_000,
    #     "tuple_sel": 0.1,
    #     "lhs_number": 7,
    #     "rhs_sel": 0.01, 
    #     "dist_params": {
    #         "dist_type": "zipf", 
    #         "lhs_dist_alpha": 1.01, 
    #         "lhs_dist_beta": 0, 
    #         "rhs_dist_alpha": 1.01, 
    #         "rhs_dist_beta": 0,
    #         "noise": 0.01,
    #         "n_type": "copy",
    #     }
    # },
]

if __name__ == "__main__":
    main()
     
