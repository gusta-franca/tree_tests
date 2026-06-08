import subprocess

def cpp_metrics(filepath: str, lhs: list[str], rhs: str,  binary_name: str, algo: str = "auto"):
    
    binary_path = f"build/bin/{binary_name}" 
    
    lhs_str = ",".join(lhs)
    
    cmd = [binary_path, filepath, lhs_str, rhs, algo]
    
    result = subprocess.run(cmd, capture_output = True, text = True)
    
    output = result.stdout.strip().split('\n')[-1] # Get last printed line
    parts = output.split(',')
    
    return {
        "mu_plus": float(parts[0]),
        "rfi_prime_plus": float(parts[1]),
        "build_time_s": float(parts[2]),
        "compute_time_s": float(parts[3]),
        "memory_used_mb": float(parts[4])
    }