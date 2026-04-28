import copy
import logging
import random
from typing import Any, Dict, List
from matplotlib import pyplot as plt
import numpy as np
import pandas as pd

from src.benchmark.plot import plot_rank_frequency


def generate_tuples(settings: Dict[str, Any]) -> pd.DataFrame:
    
    num_rows = settings["tuples"]    
    tuple_sel = settings["tuple_sel"]
    tuple_card = max(1, int(num_rows * tuple_sel))
    
    lhs_values = (settings["lhs_distribution"](size = num_rows, n = tuple_card))
    tuple_ids = (lhs_values * tuple_card).astype(np.int32)
    
    df = pd.DataFrame({"tuple_id": tuple_ids})
    
    unique_lhs = df.drop_duplicates(subset = ["tuple_id"]).reset_index(drop = True)
    num_unique_lhs = len(unique_lhs)
    
    rhs_card = max(1, int(num_rows * settings["rhs_sel"]))
    rhs_dist = settings["rhs_distribution"](size = num_unique_lhs, n = rhs_card)
    unique_lhs["rhs"] = (rhs_dist * rhs_card).astype(np.int32)
    
    df = df.merge(unique_lhs, on = "tuple_id", how = "left")
    
    # plot_rank_frequency(df)

    lhs_number = settings["lhs_number"]
    
    # Splits the tuple id into multiple columns if there is more than 1 LHS attribute
    if lhs_number > 1:
        base = int(np.ceil(num_unique_lhs ** (1.0 / lhs_number)))
        for i in range(lhs_number):
            df[f"lhs_{i}"] = (df["tuple_id"] // (base ** i)) % base
            
        df = df.drop(columns = ["tuple_id"])
    else:
        df = df.rename(columns = {"tuple_id": "lhs_0"})
    
    # Reorder columns so RHS is always at the very end
    lhs_cols = [col for col in df.columns if col != "rhs"]
    df = df[lhs_cols + ["rhs"]]
        
    return df
    

def get_noise_potential(df: pd.DataFrame) -> float:
    
    lhs_columns = [column for column in df.columns if column != 'rhs']
    
    counts = df.groupby(lhs_columns).size()
    
    max_potential_rows = np.sum(counts // 2)
    
    return float(max_potential_rows / len(df))


def potential_noisy_indices(df: pd.DataFrame, noisy_k: int) -> pd.DataFrame:
    # """
    # Identify all LHS values that bear potential to introduce noise into the dataset df. Returns a list of noisy_k indices from df that can be used to introduce noise. Use this list to iterate through it and change the tuple according to some method for introducing noise.
    # """
    
    lhs_columns = [column for column in df.columns if column != 'rhs']
    
    counts = df.groupby(lhs_columns).size()
    
    budget = counts // 2
    mask = budget > 0
    
    candidates = counts[mask]
    weights = budget[mask].astype(np.float64)
    
    probabilities = weights / np.sum(weights)
    
    lhs_values = np.random.choice(
        candidates.index, 
        size = noisy_k, 
        replace = True, 
        p = probabilities
    )
    
    return pd.DataFrame(lhs_values.tolist(), columns = lhs_columns)


def introduce_noise(settings: Dict[str, Any], df: pd.DataFrame) -> pd.DataFrame:
    """
    Introduce noise to a dataset.
    To generate noise, identify all LHS values that occur at least twice. Get the frequency table of those values, take the half of it and sample a list of LHS values (which can occur multiple times) where noise will be introduced into. Identify tuples for each LHS value, and change their RHS value by picking randomly from all other possible RHS values (i.e. from all tuples where the LHS value is not equal to the one of the identified tuple).
    """
    n_type = settings.get("n_type")
    if n_type == "bogus":
        return introduce_noise_bogus(settings, df)
    elif n_type == "typo":
        return introduce_noise_typo(settings, df)
    
    return introduce_noise_copy(settings, df)


def introduce_noise_copy(settings: Dict[str, Any], df: pd.DataFrame) -> Dict[int, List[int]]:
    
    size = settings["tuples"]
    noisy_k = int(settings["noise"] * size)
    
    if noisy_k == 0:
        return df
    
    X_values_df = potential_noisy_indices(df, noisy_k)
    
    lhs_tuples_list = [tuple(x) for x in X_values_df.to_numpy()]
    tuples_count = pd.Series(lhs_tuples_list).value_counts()   
     
    df_noisy = df.copy()
    indices_noisy = []
    lhs_columns = [column for column in df.columns if column != 'rhs']

    for tupl, n in tuples_count.items():
        mask = (df[lhs_columns] == tupl).all(axis = 1)
        possible_indices = df[mask].index
        
        if len(possible_indices) >= n:
            rows_noisy = np.random.choice(possible_indices, size = n, replace = False)
            indices_noisy.extend(rows_noisy)

    if indices_noisy:
        indices_noisy = np.array(indices_noisy)
        noise_values = df["rhs"].values
        df_noisy.loc[indices_noisy, "rhs"] = np.random.choice(noise_values, size = len(indices_noisy))

    return df_noisy


def introduce_noise_bogus(settings: Dict[str, Any], df: pd.DataFrame) -> pd.DataFrame:
    """Introduce noise into a clean dataset. Do it in a controlled fashion such that it the noise level set in the settings can be guranteed. The noise will be a completely new value each time, skewing the RHS frequencies heavily. After introducing noise, int(noise * tuples) new RHS values are in the dataset, each one with a frequency of 1."""
    noisy_k = int(settings["noise"] * settings["tuples"])
    if noisy_k == 0:
        return df  
        
    X_values = potential_noisy_indices(df, noisy_k)
    
    df_noisy = df.copy()
    lhs_columns = [column for column in df.columns if column != 'rhs']
    lhs_groups = df.groupby(lhs_columns).indices
    lhs_list = [tuple(x) if len(lhs_columns) > 1 else x[0] for x in X_values.to_numpy()]
    tuples_count = pd.Series(lhs_list).value_counts()
    
    indices_noisy = []
        
    for tupl, n in tuples_count.items():
        possible_indices = lhs_groups.get(tupl, [])
        
        if len(possible_indices) >= n:
            rows_noisy = np.random.choice(possible_indices, size = n, replace = False)
            indices_noisy.extend(rows_noisy)

    if indices_noisy:
        start_bogus = df['rhs'].max() + 1
        new_values = np.arange(start_bogus, start_bogus + len(indices_noisy))
        df_noisy.loc[indices_noisy, "rhs"] = new_values
            
    return df_noisy
    
   
def introduce_noise_typo(settings: Dict[str, Any], values: Dict[int, List[Any]], typos_n: int = 3) -> Dict[int, List[int]]:
    """Introduce noise into a clean dataset. Will introduce noise in a controlled fashion such that the noise level set in the settings will be guranteed. Noise is introduced by mapping each LHS values to a set of typos_n new RHS values. Those new RHS values represent typos in the data."""
    noisy_k = int(settings["noise"] * settings["tuples"])
    if noisy_k == 0:
        return values  # nothing to do
    df = pd.DataFrame(values)
    try:
        X_values = potential_noisy_indices(df, noisy_k)
    except ValueError:
        logging.error(
            f'It is not possible to introduce {settings["noise"]} noise to the dataset. Check the dataset with `get_noise_potential()` first.\n'
        )
        raise ValueError("Cannot create dataset, noise is set too high.")
    # this mapper will be used to mimick a typo for all values of Y
    typo_mapper = {}
    for v in set(values[1]):
        # the idea is to append a 'random' value to the original one, as if someone tapped an extra key
        type_of_Y = type(v)
        # adding multiple typo options makes it more realistic (I think)
        typo_mapper[v] = [
            type_of_Y(str(v) + str(settings["rhs_cardinality"] + n))
            for n in range(typos_n)
        ]
    dirty_values = copy.deepcopy(values)
    for x, n in pd.Series(X_values).value_counts().items():
        # get random indices of rows with the X value we want to change
        indices_to_change = df.loc[df.iloc[:, 0] == x].sample(n=n).index
        for i, x_index in enumerate(indices_to_change):
            dirty_values[1][x_index] = random.choice(
                typo_mapper[dirty_values[1][x_index]]
            )
    return dirty_values


def create_zipf_lambda(a: float):
    def generate_bounded_zipf(size: int, n: int):
        samples = []
        while len(samples) < size:
            batch = np.random.zipf(a, size=size)
            valid = batch[batch <= n]
            samples.extend(valid - 1)
        
        result = np.array(samples[:size], dtype = np.float64)
        
        return result / n 

    return generate_bounded_zipf


def create_beta_lambda(a, b):
    return lambda size, n: np.random.beta(a, b, size = size)

# TODO: verify settings.fd and remove it if safe
def generate_SYN(settings: Dict[str, Any]) -> pd.DataFrame:
    """The main method to genreate a SYN dataset. Summarizes almost all methods above."""
    
    if settings["dist_type"] == "zipf":
        settings["lhs_distribution"] = create_zipf_lambda(settings["lhs_dist_alpha"])
        settings["rhs_distribution"] = create_zipf_lambda(settings["rhs_dist_alpha"])
    else:
        settings["lhs_distribution"] = create_beta_lambda(settings["lhs_dist_alpha"], settings["lhs_dist_beta"])
        settings["rhs_distribution"] = create_beta_lambda(settings["rhs_dist_alpha"], settings["rhs_dist_beta"])
    
    df_clean = generate_tuples(settings = settings)
    
    noise = settings["noise"]
    
    if noise > 0:
        if get_noise_potential(df_clean) < noise:
            raise ValueError(f"Could not generate noise for these settings.")
        
        df_noisy = introduce_noise(settings, df_clean)

        return df_noisy

    return df_clean
