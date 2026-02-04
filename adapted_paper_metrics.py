import math
from typing import Any, List
import numpy as np
import pandas as pd
import math
from subprocess import PIPE



def mu(df: pd.DataFrame, lhs: List[Any], rhs: Any) -> float:
    """This measure is mu as defined by [Piatetsky-Shapiro & Matheus, 1993], adapted for lists of columns."""
    pdepXY = pdep(df, lhs, rhs)  # Usa a função pdep adaptada
    pdepY = pdep_self(df, rhs)  # Usa a função pdep_self que não precisa ser adaptada
    r_size = df.shape[0]

    domX_size = df.loc[:, lhs].drop_duplicates().shape[0]

    # if the fd's LHS is a key, rsize=domx_size, therefore there will be a div / 0
    if r_size == domX_size:
        return {"result": 1.0, "is_key": True}

    return {
        "result": 1.0
        - ((1 - pdepXY) / (1 - pdepY)) * ((r_size - 1) / (r_size - domX_size)),
        "is_key": False,
    }


def mu_plus(df: pd.DataFrame, lhs: List[Any], rhs: Any) -> float:
    """This measure is mu as defined by [Piatetsky-Shapiro & Matheus, 1993], adapted for lists of columns."""
    pdepXY = pdep(df, lhs, rhs)  # Usa a função pdep adaptada
    pdepY = pdep_self(df, rhs)  # Usa a função pdep_self que não precisa ser adaptada
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


def pdep_self(df: pd.DataFrame, y: Any) -> float:
    """This measure is pdep(Y) as defined by [Piatetsky-Shapiro & Matheus, 1993]."""

    return (df.loc[:, y].value_counts() / df.shape[0]).pow(2).sum()


def pdep(df: pd.DataFrame, lhs: List[Any], rhs: Any) -> float:
    """This measure is pdep as defined by [Piatetsky-Shapiro & Matheus, 1993], adapted for lists of columns."""

    combined_columns = lhs + [rhs]

    xy_counts = df.loc[:, combined_columns].value_counts().reset_index()
    xy_counts.columns = combined_columns + ["xy_count"]

    x_counts = df.loc[:, lhs].value_counts().reset_index()
    x_counts.columns = lhs + ["x_count"]
    
    counts = xy_counts.merge(x_counts, on=lhs)

    return (1 / df.shape[0]) * (counts["xy_count"].pow(2) / counts["x_count"]).sum()


def reliable_fraction_of_information(
    df: pd.DataFrame, lhs: List[Any], rhs: Any
) -> float:
    """This measures reliable fraction of information as defined by [Mandros, Boley & Vreeken, 2017], adapted for lists of columns."""
    n = df.shape[0]

    x_counts = df.loc[:, lhs].value_counts()
    y_counts = df.loc[:, rhs].value_counts()

    m = 0.0

    for _, a in x_counts.items():
        comb_n_a = math.comb(n, a)

        for _, b in y_counts.items():
            k0 = max(0, a + b - n)
            p0 = math.comb(b, k0) * math.comb(n - b, a - k0) / comb_n_a

            if k0 != 0:
                m += p0 * (k0 / n) * math.log((k0 * n) / (a * b), 2)

            for k1 in range(k0 + 1, min(a, b) + 1):
                p0 = p0 * (((a - k0) * (b - k0)) / (k1 * (n - a - b + k1)))
                m += p0 * (k1 / n) * math.log((k1 * n) / (a * b), 2)
                k0 = k1

    fi_value = fraction_of_information(df, lhs, rhs)

    bias_estimator = m / (-1.0 * ((y_counts / n) * np.log2(y_counts / n)).sum())

    return {
        "result": fi_value - bias_estimator,
        "fi": fi_value,
    }


def reliable_fraction_of_information_prime_plus(
    df: pd.DataFrame, lhs: List[Any], rhs: Any
) -> float:
    """This measures reliable fraction of information as defined by [Mandros, Boley & Vreeken, 2017], adapted for lists of columns."""
    n = df.shape[0]

    x_counts = df.loc[:, lhs].value_counts()
    y_counts = df.loc[:, rhs].value_counts()

    m = 0.0

    for _, a in x_counts.items():
        comb_n_a = math.comb(n, a)

        for _, b in y_counts.items():
            k0 = max(0, a + b - n)
            p0 = math.comb(b, k0) * math.comb(n - b, a - k0) / comb_n_a

            if k0 != 0:
                m += p0 * (k0 / n) * math.log((k0 * n) / (a * b), 2)

            for k1 in range(k0 + 1, min(a, b) + 1):
                p0 = p0 * (((a - k0) * (b - k0)) / (k1 * (n - a - b + k1)))
                m += p0 * (k1 / n) * math.log((k1 * n) / (a * b), 2)
                k0 = k1

    fi_value = fraction_of_information(df, lhs, rhs)

    bias_estimator = m / (-1.0 * ((y_counts / n) * np.log2(y_counts / n)).sum())

    rfi = fi_value - bias_estimator

    rfi_prime_plus = max(rfi / (1 - bias_estimator), 0)

    return {
        "result": rfi_prime_plus,
    }


def fraction_of_information(df: pd.DataFrame, lhs: List[Any], rhs: Any) -> float:
    """This measures matches the function FD as defined by [Cavallo & Pittarelli, 1987], adapted for lists of columns."""

    r_size = df.shape[0]

    # Calcula a entropia de Shannon de Y
    y_counts = df.loc[:, rhs].value_counts().reset_index()
    y_counts.columns = [rhs, "y_count"]
    shannonY = (
        -1.0
        * ((y_counts["y_count"] / r_size) * np.log2(y_counts["y_count"] / r_size)).sum()
    )

    combined_columns = lhs + [rhs]

    xy_counts = df.loc[:, combined_columns].value_counts().reset_index()
    xy_counts.columns = combined_columns + ["xy_count"]

    x_counts = df.loc[:, lhs].value_counts().reset_index()
    x_counts.columns = lhs + ["x_count"]

    counts = xy_counts.merge(x_counts, on=lhs)

    shannonYX = (
        -1.0
        * (
            (counts["xy_count"] / r_size)
            * np.log2((counts["xy_count"] / r_size) / (counts["x_count"] / r_size))
        ).sum()
    )

    return (shannonY - shannonYX) / shannonY


def g1(df: pd.DataFrame, lhs: List[Any], rhs: Any) -> float:
    """This measure is g_1 as proposed by [Kivinen & Mannila, 1995], adapted for lists of columns."""

    combined_columns = lhs + [rhs]

    xy_counts = df.loc[:, combined_columns].value_counts().reset_index()
    xy_counts.columns = combined_columns + ["xy_count"]

    x_counts = df.loc[:, lhs].value_counts().reset_index()
    x_counts.columns = lhs + ["x_count"]

    counts = xy_counts.merge(x_counts, on=lhs)

    violating_tuple_pairs = (
        (counts["xy_count"]) * (counts["x_count"] - counts["xy_count"])
    ).sum()

    return {"result": 1.0 - (violating_tuple_pairs / (df.shape[0] ** 2))}
    

def g2(df: pd.DataFrame, lhs: List[Any], rhs: Any) -> float:
    """This measure is g_2 as proposed by [Kivinen & Mannila, 1995], adapted for lists of columns."""
    combined_columns = lhs + [rhs]

    xy_counts = df.loc[:, combined_columns].value_counts().reset_index()
    xy_counts.columns = combined_columns + ["xy_count"]

    x_counts = df.loc[:, lhs].value_counts().reset_index()
    x_counts.columns = lhs + ["x_count"]

    counts = xy_counts.merge(x_counts, on=lhs)

    violation_participating_tuples = counts[counts["x_count"] > counts["xy_count"]]

    return {
        "result": 1.0
        - ((violation_participating_tuples["xy_count"] / df.shape[0]).sum())
    }


def g3(df: pd.DataFrame, lhs: List[Any], rhs: Any) -> float:
    """This measure is g_3 as proposed by [Kivinen & Mannila, 1995]"""

    combined_columns = lhs + [rhs]

    xy_counts = df.loc[:, combined_columns].value_counts().reset_index()

    xy_counts.columns = combined_columns + ["xy_count"]

    x_groups = xy_counts.groupby(lhs)["xy_count"]

    minimum_deletions_needed = (x_groups.sum() - x_groups.max()).sum()

    return {"result": 1.0 - (minimum_deletions_needed / df.shape[0])}


def g3_prime(df: pd.DataFrame, lhs: List[Any], rhs: Any) -> float:
    """Normalized g3' measure with baselines as proposed by Gianella & Robertson."""

    combined_columns = lhs + [rhs]

    # Get the number of distinct lhs tuples — |dom(X)|
    domX_size = df[lhs].drop_duplicates().shape[0]

    # Count the occurrences of each (X, Y) combination
    xy_counts = df.loc[:, combined_columns].value_counts().reset_index()
    xy_counts.columns = combined_columns + ["xy_count"]

    # Group by X to get all Y-values per X
    x_groups = xy_counts.groupby(lhs)["xy_count"]

    # Compute the number of tuples to remove to make X → Y hold (keep only max Y per X)
    minimum_deletions_needed = (x_groups.sum() - x_groups.max()).sum()

    # Compute |R'| as total - deletions
    r_prime_size = df.shape[0] - minimum_deletions_needed
    r_size = df.shape[0]

    # Apply the g3' formula: (|R'| - |dom(X)|) / (|R| - |dom(X)|)
    denominator = r_size - domX_size

    # Avoid division by zero in case domX == total rows
    if denominator == 0:
        return {"result": 1.0 if r_prime_size == r_size else 0.0}

    normalized_result = (r_prime_size - domX_size) / denominator

    return {"result": normalized_result}