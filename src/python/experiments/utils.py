import sys
from typing import Any, Dict

# used to join lhs fields into a single string; | was chosen since no dataset contains | as part of their columns' labels
LHS_JOIN = "|"

PYTHON_METRICS = ["mu_plus", "rfi_prime_plus", "g1", "g2", "g3_prime"]
PYTHON_EXECUTABLE = sys.executable

NAIVE_METRICS = {
    "naive_mu_test": "naive_mu",
    "naive_rfi_test": "naive_rfi",
    "naive_g1_test": "naive_g1",
    "naive_g2_test": "naive_g2",
    "naive_g3_prime_test": "naive_g3_prime",
}

def format_violation_rows(violation_rows: str) -> str:
    return violation_rows.strip("[]").replace(" ", "")

def scenario_tags(scenario: Dict[str, Any]) -> Dict[str, Any]:
    dist = scenario["dist_params"]
    return {
        "source": "synthetic",
        "tuples": scenario["tuples"],
        "tuple_sel": scenario["tuple_sel"],
        "lhs_number": scenario["lhs_number"],
        "rhs_sel": scenario.get("rhs_sel"),
        "dist_type": dist["dist_type"],
        "lhs_dist_alpha": dist["lhs_dist_alpha"],
        "lhs_dist_beta": dist["lhs_dist_beta"],
        "rhs_dist_alpha": dist["rhs_dist_alpha"],
        "rhs_dist_beta": dist["rhs_dist_beta"],
        "noise": dist["noise"],
        "n_type": dist["n_type"],
    }
