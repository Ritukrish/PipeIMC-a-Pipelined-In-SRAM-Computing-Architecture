#!/usr/bin/env python3
"""Export simulator logs to CSV and render dependency-free SVG charts."""

import argparse
import csv
import math
import re
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter

DESIGN_SHADES = ("#111111", "#555555", "#999999", "#d9d9d9")
DESIGN_LABELS = ("Baseline", "Pipelined", "Renaming", "Fine-grained")
DESIGN_ROW = re.compile(
    r"^\s*(Design [1-4]: .+?)\s+(\d+)\s+([\d.]+)x(?:\s+([\d.]+))?\s*$",
    re.MULTILINE,
)


def parse_logs(results_dir):
    rows = []
    summary_paths = sorted(results_dir.glob("pipeimc_summary_*.log"))
    if not summary_paths:
        raise RuntimeError(f"No simulator summary logs found in {results_dir}")

    for summary_path in summary_paths:
        text = summary_path.read_text(encoding="utf-8")
        kernel_match = re.search(r"^Kernel\s*:\s*(\S+)", text, re.MULTILINE)
        operations_match = re.search(
            r"^Total Operations\s*:\s*(\d+)", text, re.MULTILINE
        )
        if not kernel_match or not operations_match:
            raise RuntimeError(f"Could not read kernel/operation count from {summary_path}")

        kernel = kernel_match.group(1)
        operations = int(operations_match.group(1))
        cycle_path = results_dir / f"pipeimc_cycle_{kernel}.log"
        cycle_text = cycle_path.read_text(encoding="utf-8") if cycle_path.exists() else ""
        paper_shape_match = re.search(
            r"^Paper workload size\s*:\s*(.+)$", text, re.MULTILINE
        )
        sample_shape_match = re.search(
            r"^Simulated sample\s*:\s*(.+)$", text, re.MULTILINE
        )
        checks = {
            design: result
            for design, result in re.findall(
                r"^\s*(Design [1-4]: .+?):\s+(PASS|FAIL)",
                text,
                re.MULTILINE,
            )
        }

        for design, cycles_text, speedup_text, runtime_text in DESIGN_ROW.findall(text):
            cycles = int(cycles_text)
            cycles_per_operation = cycles / operations if operations else 0.0
            design_section = re.search(
                rf"################ {re.escape(design)} ################(.*?)(?=\n################ |\Z)",
                cycle_text,
                re.DOTALL,
            )
            section = design_section.group(1) if design_section else ""
            rows.append(
                {
                    "kernel": kernel,
                    "reference_workload_shape": (
                        paper_shape_match.group(1) if paper_shape_match else ""
                    ),
                    "simulated_sample_shape": (
                        sample_shape_match.group(1) if sample_shape_match else ""
                    ),
                    "design": design,
                    "operations": operations,
                    "cycles": cycles,
                    "speedup": float(speedup_text),
                    "runtime_ms": float(runtime_text) if runtime_text else None,
                    "cycles_per_operation": cycles_per_operation,
                    "operations_per_cycle": operations / cycles if cycles else 0.0,
                    "load_phases": len(re.findall(r"\[LOAD START\]", section)),
                    "calculation_phases": len(re.findall(r"\[CALC START\]", section)),
                    "store_phases": len(re.findall(r"\[STORE START\]", section)),
                    "correctness": checks.get(design, "UNKNOWN"),
                }
            )

    if not rows:
        raise RuntimeError("No design summaries were parsed from the log files")
    return rows


def write_csvs(results_dir, rows):
    fields = (
        "kernel",
        "reference_workload_shape",
        "simulated_sample_shape",
        "design",
        "operations",
        "cycles",
        "speedup",
        "runtime_ms",
        "cycles_per_operation",
        "operations_per_cycle",
        "load_phases",
        "calculation_phases",
        "store_phases",
        "correctness",
    )
    with (results_dir / "metrics.csv").open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(
            output, fieldnames=fields, lineterminator="\n"
        )
        writer.writeheader()
        writer.writerows(rows)

    for metric, filename in (
        ("cycles", "cycles.csv"),
        ("speedup", "speedup.csv"),
        ("runtime_ms", "runtime.csv"),
        ("operations_per_cycle", "throughput.csv"),
    ):
        with (results_dir / filename).open(
            "w", newline="", encoding="utf-8"
        ) as output:
            writer = csv.writer(output, lineterminator="\n")
            writer.writerow(("kernel", *DESIGN_LABELS))
            for kernel in sorted({row["kernel"] for row in rows}):
                by_design = {row["design"]: row[metric] for row in rows if row["kernel"] == kernel}
                writer.writerow(
                    (
                        kernel,
                        *(
                            by_design.get(f"Design {index}: {label}", "")
                            for index, label in enumerate(
                                (
                                    "In-Order Baseline",
                                    "Pipelined",
                                    "Pipelined + Renaming",
                                    "Fine-Grained Issue",
                                ),
                                start=1,
                            )
                        ),
                    )
                )


def write_bar_chart(
    results_dir, rows, metric, filename, title, unit, log_scale=False
):
    kernels = sorted({row["kernel"] for row in rows})
    values = [row[metric] for row in rows if row[metric] is not None]
    if not values:
        return False

    if log_scale:
        values = [value for value in values if value > 0]
        if not values:
            return False
        baseline = 10 ** math.floor(math.log10(min(values)))
    else:
        baseline = 0

    designs = (
        "Design 1: In-Order Baseline",
        "Design 2: Pipelined",
        "Design 3: Pipelined + Renaming",
        "Design 4: Fine-Grained Issue",
    )
    row_lookup = {
        (row["kernel"], row["design"]): row[metric]
        for row in rows
        if row[metric] is not None and (not log_scale or row[metric] > 0)
    }

    fig, ax = plt.subplots(figsize=(17, 9))
    x_positions = list(range(len(kernels)))
    group_width = 0.8
    bar_width = group_width / len(designs)
    for design_index, (design, label) in enumerate(zip(designs, DESIGN_LABELS)):
        offsets = [
            x + (design_index - (len(designs) - 1) / 2) * bar_width
            for x in x_positions
        ]
        design_values = [row_lookup.get((kernel, design)) for kernel in kernels]
        bars = ax.bar(
            offsets,
            [
                value - baseline if value is not None else 0
                for value in design_values
            ],
            width=bar_width * 0.92,
            bottom=baseline,
            color=DESIGN_SHADES[design_index],
            edgecolor="black",
            linewidth=0.8,
            label=label,
        )
        for bar, value in zip(bars, design_values):
            if value is None:
                bar.set_visible(False)
                continue
            # ax.annotate(
            #     format_bar_value(value, metric),
            #     (bar.get_x() + bar.get_width() / 2, baseline + bar.get_height()),
            #     xytext=(0, 4),
            #     textcoords="offset points",
            #     ha="center",
            #     va="bottom",
            #     rotation=90,
            #     fontsize=25,
            #     bbox={"facecolor": "white", "edgecolor": "none", "pad": 1.5},
            #     clip_on=False,
            # )

    if log_scale:
        ax.set_yscale("log")
        ax.set_ylim(bottom=baseline, top=max(values) * 2)
    else:
        ax.set_ylim(bottom=0, top=max(values) * 1.35 or 1)
    if metric == "cycles":
        ax.yaxis.set_major_formatter(
            FuncFormatter(lambda value, _: format_cycle_count(value))
        )
    ax.set_xticks(x_positions, kernels)
    ax.set_ylabel(unit, fontsize=25, labelpad=12)
    ax.set_title(title, fontsize=25, pad=22)
    ax.tick_params(axis="both", labelsize=17, pad=8)
    ax.grid(axis="y", color="#b0b0b0", linewidth=0.7, alpha=0.7)
    ax.set_axisbelow(True)
    ax.legend(fontsize=25, ncol=len(designs), loc="upper center",
              bbox_to_anchor=(0.5, -0.10), frameon=False)
    fig.tight_layout()
    fig.savefig(results_dir / filename, format="pdf", bbox_inches="tight")
    plt.close(fig)
    return True


def format_bar_value(value, metric):
    if metric == "cycles":
        return format_cycle_count(value)
    if metric == "runtime_ms":
        return f"{value:.2g}"
    return f"{value:.2f}"


def format_cycle_count(value):
    if value >= 100_000:
        thousands = value / 1_000
        if thousands.is_integer():
            return f"{thousands:.0f}k"
        return f"{thousands:.1f}k"
    return f"{value:,.0f}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--results-dir",
        type=Path,
        default=Path("results"),
        help="directory containing pipeimc summary and cycle logs (default: results)",
    )
    args = parser.parse_args()
    results_dir = args.results_dir

    try:
        rows = parse_logs(results_dir)
        write_csvs(results_dir, rows)
        print(f"CSV metrics written to {results_dir / 'metrics.csv'}")
        for metric, filename, title, unit, log_scale in (
            ("cycles", "cycles.pdf", "Simulation cycles by workload (log scale)", "Cycles (log scale, lower is better)", True),
            ("speedup", "speedup.pdf", "Speedup vs. in-order baseline", "Speedup (higher is better)", False),
            ("runtime_ms", "runtime.pdf", "Measured host execution time by workload (log scale)", "Host time in ms (log scale)", True),
            ("operations_per_cycle", "throughput.pdf", "Operations per simulated cycle", "Operations per cycle (higher is better)", False),
        ):
            if write_bar_chart(
                results_dir,
                rows,
                metric,
                filename,
                title,
                unit,
                log_scale,
            ):
                print(f"Graph written to {results_dir / filename}")
            else:
                print(f"Skipped {filename}: no {metric} data in the logs", file=sys.stderr)
    except (OSError, RuntimeError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
