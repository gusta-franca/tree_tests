import pandas as pd
import matplotlib.pyplot as plt

def plot_rank_frequency(df: pd.DataFrame, save_path: str = None, show: bool = True):

    num_cols = len(df.columns)
    
    fig, axes = plt.subplots(1, num_cols, figsize=(6 * num_cols, 5))
    
    if num_cols == 1:
        axes = [axes]
        
    for i, col in enumerate(df.columns):
        counts = df[col].value_counts().values
        ranks = range(1, len(counts) + 1)
        
        axes[i].plot(ranks, counts, marker='.', linestyle='none', markersize=4, color='royalblue')
        
        axes[i].set_title(f'Rank-Frequency: {col}')
        axes[i].set_xlabel('Rank (Log Scale)')
        axes[i].set_ylabel('Frequency (Log Scale)')
        
        axes[i].set_xscale('log')
        axes[i].set_yscale('log')
        axes[i].grid(True, which="both", ls="--", alpha=0.5)
        
    plt.tight_layout()
    
    if save_path:
        plt.savefig(save_path)
        print(f"Plot saved successfully to '{save_path}'")
        
    if show:
        plt.show()
    else:
        plt.close(fig)
