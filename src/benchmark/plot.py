import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

def plot_rank_frequency(df: pd.DataFrame, bins: int = 50, log_scale: bool = True, title: str = None):
    lhs_columns = [c for c in df.columns if c != "rhs"]

    lhs_counts = df.groupby(lhs_columns).size().sort_values(ascending=False)
    rhs_counts = df["rhs"].value_counts().sort_values(ascending=False)

    fig, axes = plt.subplots(2, 2, figsize=(12, 8))

    ax = axes[0, 0]
    ax.plot(np.arange(1, len(lhs_counts) + 1), lhs_counts.values, marker=".", linestyle="none")
    ax.set_xlabel("LHS group rank")
    ax.set_ylabel("row count")
    ax.set_title("LHS group size by rank")
    if log_scale:
        ax.set_xscale("log")
        ax.set_yscale("log")

    ax = axes[0, 1]
    ax.hist(lhs_counts.values, bins=bins)
    ax.set_xlabel("rows per LHS group")
    ax.set_ylabel("number of LHS groups")
    ax.set_title("LHS group size distribution")

    ax = axes[1, 0]
    ax.plot(np.arange(1, len(rhs_counts) + 1), rhs_counts.values, marker=".", linestyle="none")
    ax.set_xlabel("RHS value rank")
    ax.set_ylabel("row count")
    ax.set_title("RHS value frequency by rank")
    if log_scale:
        ax.set_xscale("log")
        ax.set_yscale("log")

    ax = axes[1, 1]
    ax.hist(rhs_counts.values, bins=bins)
    ax.set_xlabel("rows per RHS value")
    ax.set_ylabel("number of RHS values")
    ax.set_title("RHS value frequency distribution")

    if title:
        fig.suptitle(title)

    fig.tight_layout()
    plt.show()

    return fig
