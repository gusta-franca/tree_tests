import json
import subprocess


def cpp_metrics(csv_filepath: str, lhs: list[str], rhs: str,  binary_name: str, algo: str = "auto"):
    
    binary_path = f"build/bin/{binary_name}" 
    
    lhs_str = ",".join(lhs)
    
    cmd = [binary_path, csv_filepath, lhs_str, rhs, algo]
    
    result = subprocess.run(cmd, capture_output = True, text = True)

    metrics = {}

    for line in result.stdout.split('\n'):
        if line.startswith("HLL_JSON:"):
            json_string = line.replace("HLL_JSON:", "").strip()
            hll_data = json.loads(json_string)
            
            print(f"hll_xy : {hll_data['hll_xy_time_s']}s")
            print(f"hll_col: {hll_data['hll_col_time_s']}s\n")
            
        elif line.startswith("RESULT_JSON:"):
            json_string = line.replace("RESULT_JSON:", "").strip()
            metrics = json.loads(json_string)

    return metrics;
