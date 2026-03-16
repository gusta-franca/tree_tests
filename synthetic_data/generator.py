import copy
import logging
import random
from typing import Any, Dict, List

import numpy as np
import pandas as pd


def generate_tuples(settings: Dict[str, Any]):
    
    num_rows = settings["tuples"]
    lhs_sels = settings["lhs_sels"] # Guaranteed to be a list, even on 1-to-1 FDs.
    
    lhs_columns = []
    for sel in lhs_sels:
        card = int(num_rows * sel)
        column = (settings["lhs_distribution"](size = num_rows, n = card) * card).astype(np.int32)
        lhs_columns.append(column)
        
    rhs_card = int(num_rows * settings["rhs_sel"])
    rhs_dist = (settings["rhs_distribution"](size = num_rows, n = rhs_card) * rhs_card).astype(np.int32)
    rhs_index = 0 

    rhs_data = []
    fd_dict = {}
    
    for r in range(num_rows):
        row_key = tuple(column[r] for column in lhs_columns)
        
        if row_key not in fd_dict:
            fd_dict[row_key] = rhs_dist[rhs_index]
            rhs_index += 1 
              
        rhs_data.append(fd_dict[row_key])
    
    df_dict = {f"lhs_{i}": column for i, column in enumerate(lhs_columns)} # Molding it as a pandas dataframe.
    df_dict["rhs"] = rhs_data
    
    return pd.DataFrame(df_dict)


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
    
    return pd.DataFrame(lhs_values, columns = lhs_columns)


def introduce_noise(
    settings: Dict[str, Any], df: pd.DataFrame
) -> pd.DataFrame:
    """
    Introduce noise to a dataset.
    To generate noise, identify all LHS values that occur at least twice. Get the frequency table of those values, take the half of it and sample a list of LHS values (which can occur multiple times) where noise will be introduced into. Identify tuples for each LHS value, and change their RHS value by picking randomly from all other possible RHS values (i.e. from all tuples where the LHS value is not equal to the one of the identified tuple).
    """
    return introduce_noise_copy(settings, df)


def introduce_noise_copy(
    settings: Dict[str, Any], df: pd.DataFrame
) -> Dict[int, List[int]]:
    
    size = settings["tuples"]
    noisy_k = int(settings["noise"] * size)
    
    if noisy_k == 0:
        return df
    
    tuples_count = potential_noisy_indices(df, noisy_k).count()
    
    df_noisy = df.copy()
    indices_noisy = []
    lhs_columns = [column for column in df.columns if column != 'rhs']

    for tuple, n in tuples_count.items():
        mask = (df[lhs_columns] == tuple).all(axis = 1)
        possible_indices = df[mask].index
        
        if len(possible_indices) >= n:
            rows_noisy = np.random.choice(possible_indices, size = n, replace = False)
            indices_noisy.extend(rows_noisy)

    if indices_noisy:
        indices_noisy = np.array(indices_noisy)
        noise_values = df["rhs"].values
        df_noisy.loc[indices_noisy, "rhs"] = np.random.choice(noise_values, size = len(indices_noisy))

    return df_noisy


def introduce_noise_bogus(
    settings: Dict[str, Any], values: Dict[int, List[int]]
) -> Dict[int, List[int]]:
    """Introduce noise into a clean dataset. Do it in a controlled fashion such that it the noise level set in the settings can be guranteed. The noise will be a completely new value each time, skewing the RHS frequencies heavily. After introducing noise, int(noise * tuples) new RHS values are in the dataset, each one with a frequency of 1."""
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
    dirty_values = copy.deepcopy(values)
    bogus_value = df.iloc[:, 1].nunique()  # starting at nunique is just the best guess
    for x, n in pd.Series(X_values).value_counts().items():
        # get random indices of rows with the X value we want to change
        indices_to_change = df.loc[df.iloc[:, 0] == x].sample(n=n).index
        for i, x_index in enumerate(indices_to_change):
            # make sure that our bogus value is not already in the Y values
            while bogus_value in dirty_values[1]:
                bogus_value += 1
            dirty_values[1][x_index] = bogus_value
    return dirty_values
    
   
def introduce_noise_typo(
    settings: Dict[str, Any], values: Dict[int, List[Any]], typos_n: int = 3
) -> Dict[int, List[int]]:
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
        # Keep generating numbers until we have enough valid ones
        while len(samples) < size:
            # Generate a raw batch
            batch = np.random.zipf(a, size=size)
            # Filter out the massive numbers, keep only those <= n
            valid = batch[batch <= n]
            # Subtract 1 so our domain starts at 0
            samples.extend(valid - 1)
        
        # Take exactly the number of samples requested
        result = np.array(samples[:size], dtype=np.float64)
        
        # Scale to [0.0, 1.0) so it matches the behavior of the Beta distribution
        return result / n 

    return generate_bounded_zipf


def create_beta_lambda(a, b):
    return lambda size, n: np.random.beta(a, b, size = size)

def generate_SYN(
    fd: bool,
    tuples: int,
    lhs_sels: List[float],
    rhs_sel: float,
    lhs_dist_alpha: float,
    lhs_dist_beta: float,
    rhs_dist_alpha: float,
    rhs_dist_beta: float,
    noise: float = 0.01,
    n_type: str = "copy",
    dist_type: str = "beta"     
) -> pd.DataFrame:
    """The main method to genreate a SYN dataset. Summarizes almost all methods above."""
    
    if dist_type == "zipf":
        lhs_func = create_zipf_lambda(lhs_dist_alpha)
        rhs_func = create_zipf_lambda(rhs_dist_alpha)
    else:
        lhs_func = create_beta_lambda(lhs_dist_alpha, lhs_dist_beta)
        rhs_func = create_beta_lambda(rhs_dist_alpha, rhs_dist_beta)
        
    settings = {
        "tuples": tuples,
        "lhs_sels": lhs_sels,
        "rhs_sel": rhs_sel,
        "lhs_distribution": lhs_func,
        "rhs_distribution": rhs_func,
        "noise": noise
    }
    
    df_clean = generate_tuples(settings = settings)
    
    if fd and noise > 0:
        if get_noise_potential(df_clean) < settings["noise"]:
            raise ValueError(f"Could not generate noise for these settings.")
        
        if n_type == "copy":
            data = introduce_noise_copy(settings, df_clean)
        elif n_type == "bogus":
            data = introduce_noise_bogus(settings, df_clean)
        elif n_type == "typo":
            data = introduce_noise_typo(settings, df_clean)
        
        return data

    return df_clean
