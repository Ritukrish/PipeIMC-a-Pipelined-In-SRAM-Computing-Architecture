
#!/usr/bin/env python3

import subprocess
import re
import os
import glob
import sys

kernels = ["matmul", "stencil", "kmeans"]
iterations = "256"

results = {}
failed_kernels = []

# --------------------------------------------------
# STEP 1: CLEAN PREVIOUS BUILD FILES
# --------------------------------------------------

print("[Step 1/3] Cleaning previous build files...")

for file in glob.glob("*.o") + [
    "pipeimc_sim.exe",
    "pipeimc_sim"
]:
    if os.path.exists(file):
        try:
            os.remove(file)
        except OSError as e:
            print(f"Warning: Could not remove {file}: {e}")

# --------------------------------------------------
# STEP 2: COMPILE SIMULATOR
# --------------------------------------------------

print("\n[Step 2/3] Compiling simulator with g++...")

compile_cmd = [
    "g++", "-O3", "-std=c++17", "-Wall", "-Wextra",
    "src/main.cpp",
    "src/sim_engine.cpp",
    "src/model_baseline.cpp",
    "src/model_pipelined.cpp",
    "src/model_renaming.cpp",
    "src/model_fine_grained.cpp",
    "src/trace_generator.cpp",
    "-o", "pipeimc_sim.exe"
]

try:
    subprocess.run(
        compile_cmd,
        check=True,
        text=True
    )

    print("Compilation successful!")

except subprocess.CalledProcessError as e:
    print("\nCompilation failed.")
    print(e)
    sys.exit(1)

except FileNotFoundError:
    print("Error: g++ was not found. Check your PATH.")
    sys.exit(1)

# --------------------------------------------------
# STEP 3: RUN ALL KERNELS
# --------------------------------------------------

print("\n[Step 3/3] Running simulations for all kernels...")

exe_name = os.path.abspath("pipeimc_sim.exe")



def extract_cycle_count(output, design_number):
    """
    Extract cycle count from:
    Design 1: In-Order Baseline    87297    1.00x
    """

    for line in output.splitlines():

        line = line.strip()

        if line.startswith(f"Design {design_number}:"):

            # Get everything after the design label.
            remaining = line.split(":", 1)[1]

            # Extract numeric values.
            numbers = re.findall(r"\d+", remaining)

            if numbers:
                # First number is the cycle count.
                return int(numbers[0])

    return None

for kernel in kernels:

    print("\n" + "=" * 55)
    print(f" Executing Kernel: {kernel.upper()}")
    print("=" * 55)

    try:
        process = subprocess.run(
            [exe_name, kernel, iterations],
            capture_output=True,
            text=True,
            check=False
        )

        output = process.stdout

        # Display simulator output.
        print(output)

        if process.returncode != 0:
            print(
                f"[ERROR] Simulator failed for {kernel}."
            )

            if process.stderr:
                print("Error details:")
                print(process.stderr)

            failed_kernels.append(kernel)
            continue

        # Extract cycles for all four designs.
        cycles = {}

        for design_number in range(1, 5):
            cycles[design_number] = extract_cycle_count(
                output,
                design_number
            )

        # Validate extracted values.
        if any(value is None for value in cycles.values()):
            print(
                f"[ERROR] Could not extract all cycle counts "
                f"for {kernel}."
            )

            print("Extracted values:", cycles)
            print("Raw simulator output:")
            print(output)

            failed_kernels.append(kernel)
            continue

        c1 = cycles[1]
        c2 = cycles[2]
        c3 = cycles[3]
        c4 = cycles[4]

        if min(c1, c2, c3, c4) <= 0:
            print(f"[ERROR] Invalid cycle count for {kernel}.")
            failed_kernels.append(kernel)
            continue

        results[kernel] = {
            "Baseline": c1,
            "Pipelined": c2,
            "Renaming": c3,
            "Fine-Grained": c4
        }

        print(f"[SUCCESS] {kernel.upper()} results extracted.")

    except Exception as e:
        print(f"[ERROR] Unexpected error for {kernel}: {e}")
        failed_kernels.append(kernel)
        continue

# --------------------------------------------------
# FINAL SPEEDUP EVALUATION REPORT
# --------------------------------------------------

print("\n" + "#" * 95)
print(" FINAL PIPEIMC SPEEDUP EVALUATION REPORT")
print("#" * 95)

print(
    f"{'Kernel':<12} | "
    f"{'Baseline':<12} | "
    f"{'Pipelined':<19} | "
    f"{'Renaming':<19} | "
    f"{'Fine-Grained':<19}"
)

print("-" * 95)

for kernel in kernels:

    if kernel not in results:
        continue

    c1 = results[kernel]["Baseline"]
    c2 = results[kernel]["Pipelined"]
    c3 = results[kernel]["Renaming"]
    c4 = results[kernel]["Fine-Grained"]

    s2 = c1 / c2
    s3 = c1 / c3
    s4 = c1 / c4

    print(
        f"{kernel:<12} | "
        f"{c1:<12} | "
        f"{c2} ({s2:.2f}x)    | "
        f"{c3} ({s3:.2f}x)    | "
        f"{c4} ({s4:.2f}x)"
    )

print("#" * 95)

# --------------------------------------------------
# EXECUTION SUMMARY
# --------------------------------------------------

print("\nExecution Summary:")
print(f"Successful kernels: {len(results)}/{len(kernels)}")

if failed_kernels:
    print("Failed kernels:", ", ".join(failed_kernels))
else:
    print("All three kernels completed successfully!")

