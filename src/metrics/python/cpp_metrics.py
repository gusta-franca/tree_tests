import json
import subprocess


def run_cpp_binary(binary_name: str, args: list, stdin: str = None) -> dict:
    binary_path = f"build/bin/{binary_name}"
    cmd = [binary_path] + [str(a) for a in args]
    result = subprocess.run(cmd, input=stdin, capture_output=True, text=True)

    metrics = {}
    for line in result.stdout.split('\n'):
        if line.startswith("RESULT_JSON:"):
            metrics = json.loads(line.replace("RESULT_JSON:", "").strip())

    if not metrics:
        print(f"[{binary_name}] produced no RESULT_JSON "
              f"(returncode={result.returncode})\nstderr:\n{result.stderr}")

    return metrics


def format_violation_rows(violation_rows: str) -> str:
    return violation_rows.strip("[]").replace(" ", "")
