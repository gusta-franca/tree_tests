import copy
import logging
import random
from typing import Any, Dict, List

import numpy as np
import pandas as pd


def generate_tuples(settings: Dict[str, Any]):
    
    num_rows = settings["tuples"]
    lhs_sels = settings["lhs_sel"] # Guaranteed to be a list, even on 1-to-1 FDs.
    
    lhs_columns = []
    for sel in lhs_sels:
        card = int(num_rows * sel)
        column = (settings["lhs_distribution"](size = num_rows) * card).astype(np.int32)
        lhs_columns.append(column)
        
    rhs_card = int(num_rows * settings["rhs_sel"])
    rhs_dist = (settings["rhs_distribution"](size = num_rows) * rhs_card).astype(np.int32)
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


# def get_noise_potential(
#     settings: Dict[str, Any], values: Dict[int, List[int]]
# ) -> float:
#     """
#     Return the potential percentage of noise that can be introduced to a dataset.
#     Values is assumed to be a dictionary with keys 0 and 1, where 0 is the LHS and 1 is the RHS.
#     """
#     return get_noise_potential_df(pd.DataFrame(values), 0, 1)


# def get_noise_potential_df(df: pd.DataFrame, lhs: Any, rhs: Any) -> float:
#     """
#     Return the potential percentage of noise that can be introduced to a dataset.
#     """
#     df = df.loc[:, [lhs, rhs]].dropna(how="any")
#     counts = df.loc[:, lhs].value_counts()
#     potentials = counts.loc[
#         (counts // 2) > 0
#     ]  # if we change more than half the values, we are decreasing noise again
#     return (potentials // 2).sum() / df.shape[0]

def get_noise_potential(values: Dict[int, np.ndarray]) -> float:
 
    lhs_array = values[0]
    
    _, counts = np.unique(lhs_array, return_counts = True)
    
    max_potential_rows = np.sum(counts // 2)
    
    return float(max_potential_rows / len(lhs_array))


def potential_noisy_indices(values: Dict[int, np.ndarray], noisy_k: int) -> np.ndarray:
    # """
    # Identify all LHS values that bear potential to introduce noise into the dataset df. Returns a list of noisy_k indices from df that can be used to introduce noise. Use this list to iterate through it and change the tuple according to some method for introducing noise.
    # """
    # X_counts = df.iloc[:, 0].value_counts()  # counts for each generated value from X
    # # a list of potential X values to introduce noise to
    # # i.e.: X values that appear at least two times
    # potentials = X_counts.loc[(X_counts // 2) > 0] // 2
    # # from the potentials, sample the X_values that will be changed in the following
    # X_values = random.sample(
    #     potentials.index.tolist(), k=noisy_k, counts=potentials.values.tolist()
    # )
    # return X_values
    
    lhs_array = values[0]
    
    unique_vals, counts = np.unique(lhs_array, return_counts = True)
    
    budget = counts // 2
    mask = budget > 0
    
    candidates = unique_vals[mask]
    weights = budget[mask].astype(np.float64)
    
    probabilities = weights / np.sum(weights)
    
    lhs_values = np.random.choice(
        candidates, 
        size = noisy_k, 
        replace = True, 
        p = probabilities
    )
    
    return lhs_values


def introduce_noise(
    settings: Dict[str, Any], values: Dict[int, List[int]]
) -> Dict[int, List[int]]:
    """
    Introduce noise to a dataset.
    To generate noise, identify all LHS values that occur at least twice. Get the frequency table of those values, take the half of it and sample a list of LHS values (which can occur multiple times) where noise will be introduced into. Identify tuples for each LHS value, and change their RHS value by picking randomly from all other possible RHS values (i.e. from all tuples where the LHS value is not equal to the one of the identified tuple).
    """
    return introduce_noise_copy(settings, values)


def introduce_noise_copy(
    settings: Dict[str, Any], values: Dict[int, List[int]]
) -> Dict[int, List[int]]:
    # """
    # Introduce noise to a dataset.
    # To generate noise, identify all LHS values that occur at least twice. Get the frequency table of those values, take the half of it and sample a list of LHS values (which can occur multiple times) where noise will be introduced into. Identify tuples for each LHS value, and change their RHS value by picking randomly from all other possible RHS values (i.e. from all tuples where the LHS value is not equal to the one of the identified tuple).
    # """
    # noisy_k = int(settings["noise"] * settings["tuples"])
    # if noisy_k == 0:
    #     return values  # nothing to do
    # df = pd.DataFrame(values)
    # try:
    #     X_values = potential_noisy_indices(df, noisy_k)
    # except ValueError:
    #     logging.error(
    #         f'It is not possible to introduce {settings["noise"]} noise to the dataset. Check the dataset with `get_noise_potential()` first.\n'
    #     )
    #     raise ValueError("Cannot create dataset, noise is set too high.")
    # dirty_values = copy.deepcopy(values)
    # Y_counts = df.iloc[:, 1].value_counts()  # counts for each generated value from Y
    # for x, n in pd.Series(X_values).value_counts().items():
    #     y_candidates = Y_counts[
    #         Y_counts.index != x
    #     ].copy()  # get all Y values that are not x
    #     # get random indices of rows with the X value we want to change
    #     indices_to_change = df.loc[df.iloc[:, 0] == x].sample(n=n).index
    #     # an array of Y values to change the identified rows to (i.e. the noise)
    #     # choosing from existing Y values hopefully maintains the Y distribution
    #     y_values = random.choices(
    #         y_candidates.index.tolist(), k=n, weights=y_candidates.values.tolist()
    #     )
    #     for i, x_index in enumerate(indices_to_change):
    #         dirty_values[1][x_index] = y_values[i]
    # return dirty_values
    
    size = settings["tuples"]
    noisy_k = int(settings["noise"] * size)
    
    if noisy_k == 0:
        return values

    lhs = potential_noisy_indices(values, noisy_k)
    unique_x, counts_x = np.unique(lhs, return_counts = True)
    
    lhs_array = values[0]
    idx_sort = np.argsort(lhs_array)
    sorted_lhs = lhs_array[idx_sort]
   
    dirty_rhs = values[1].copy()

    indices_to_change = []
    
    for x, n in zip(unique_x, counts_x):
        start = np.searchsorted(sorted_lhs, x, side = 'left')
        end = np.searchsorted(sorted_lhs, x, side = 'right')
        
        possible_row_indices = idx_sort[start:end]
        
        if len(possible_row_indices) >= n:
            chosen_rows = np.random.choice(possible_row_indices, size = n, replace = False)
            indices_to_change.extend(chosen_rows)

    indices_to_change = np.array(indices_to_change)
    
    noise_pool = values[1]
    new_rhs_values = np.random.choice(noise_pool, size = len(indices_to_change))

    dirty_rhs[indices_to_change] = new_rhs_values

    return {0: lhs_array, 1: dirty_rhs}


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


def create_zipf_lambda(a, n):
    return lambda size = 1: (np.clip(np.random.zipf(a, size = size), 1, n) - 1) / n

def create_beta_lambda(a, b):
    return lambda size = 1: np.random.beta(a, b, size = size)


def generate_SYN(
    fd: bool,
    tuples: int,
    lhs_cardinality: int,
    rhs_cardinality: int,
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
        lhs_func = create_zipf_lambda(lhs_dist_alpha, lhs_cardinality)
        rhs_func = create_zipf_lambda(rhs_dist_alpha, rhs_cardinality)
    else:
        lhs_func = create_beta_lambda(lhs_dist_alpha, lhs_dist_beta)
        rhs_func = create_beta_lambda(rhs_dist_alpha, rhs_dist_beta)
        
    settings = {
        "tuples": tuples,
        "lhs_cardinality": lhs_cardinality,
        "rhs_cardinality": rhs_cardinality,
        "lhs_distribution": lhs_func,
        "rhs_distribution": rhs_func,
        "noise": noise
    }
    
    fd_table = None
    
    if fd:
        fd_table = assign_fds(settings = settings)
        
    clean_data = generate_tuples(settings = settings, fd_table = fd_table)
    
    if fd:
        # make sure that noise can be introduced to the clean dataset
        safeguard = 10
        while get_noise_potential(clean_data) < settings["noise"]:
            clean_data = generate_tuples(settings = settings, fd_dictionary = fd_dictionary)
            safeguard -= 1
            if safeguard == 0:
                raise ValueError(
                    f"Could not generate noise for these settings: {settings}"
                )
        
        if n_type == "copy":
            data = introduce_noise_copy(settings, clean_data)
        elif n_type == "bogus":
            data = introduce_noise_bogus(settings, clean_data)
        elif n_type == "typo":
            data = introduce_noise_typo(settings, clean_data)

    else:
        data = clean_data
    
    # transform the values to a pandas DataFrame with columns 'lhs','rhs'
    df = pd.DataFrame(data)
    df.columns = ["lhs", "rhs"]
    return df
