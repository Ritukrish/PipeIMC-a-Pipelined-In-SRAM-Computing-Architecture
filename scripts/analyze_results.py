#!/usr/bin/env python3
"""Export simulator logs to CSV and render dependency-free SVG charts."""

import argparse
import csv
import math
import re
import sys
from pathlib import Path
from xml.sax.saxutils import escape


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
        writer = csv.DictWriter(output, fieldnames=fields)
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
            writer = csv.writer(output)
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
    values = [
        row[metric]
        for row in rows
        if row[metric] is not None
    ]
    if not values:
        return False

    if log_scale:
        values = [value for value in values if value > 0]
        if not values:
            return False
        axis_min = math.floor(math.log10(min(values)))
        axis_max = math.ceil(math.log10(max(values)))
        if axis_min == axis_max:
            axis_max += 1
        value_position = lambda value: math.log10(value)
        axis_label = lambda position: f"{10 ** position:.0e}"
    else:
        axis_min = 0.0
        axis_max = max(values) * 1.12 or 1.0
        value_position = lambda value: value
        axis_label = lambda position: f"{position:.2f}"

    width = max(1080, 150 * len(kernels) + 100)
    height = 480
    left, right, top, bottom = 72, 30, 55, 112
    plot_width = width - left - right
    plot_height = height - top - bottom
    group_width = plot_width / max(len(kernels), 1)
    bar_width = min(36, group_width / 6)
    design_names = (
        "Design 1: In-Order Baseline",
        "Design 2: Pipelined",
        "Design 3: Pipelined + Renaming",
        "Design 4: Fine-Grained Issue",
    )

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        f'<text x="{width / 2}" y="28" text-anchor="middle" font-family="sans-serif" font-size="20" font-weight="bold">{escape(title)}</text>',
    ]

    for tick in range(5):
        position = axis_min + (axis_max - axis_min) * tick / 4
        y = top + plot_height * (1 - tick / 4)
        parts.append(
            f'<line x1="{left}" y1="{y:.1f}" x2="{width-right}" y2="{y:.1f}" stroke="#dddddd"/>'
        )
        parts.append(
            f'<text x="{left-10}" y="{y+4:.1f}" text-anchor="end" font-family="sans-serif" font-size="11">{axis_label(position)}</text>'
        )

    row_lookup = {
        (row["kernel"], row["design"]): row[metric]
        for row in rows
        if row[metric] is not None and (not log_scale or row[metric] > 0)
    }
    for group_index, kernel in enumerate(kernels):
        center = left + group_width * (group_index + 0.5)
        for design_index, design in enumerate(design_names):
            value = row_lookup.get((kernel, design))
            if value is None:
                continue
            x = center + (design_index - 1.5) * (bar_width + 3) - bar_width / 2
            bar_height = plot_height * (
                value_position(value) - axis_min
            ) / (axis_max - axis_min)
            y = top + plot_height - bar_height
            parts.append(
                f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_width:.1f}" height="{bar_height:.1f}" fill="{DESIGN_SHADES[design_index]}" stroke="#000000" stroke-width="0.8"/>'
            )
            parts.append(
                f'<text x="{x + bar_width/2:.1f}" y="{y-4:.1f}" text-anchor="middle" font-family="sans-serif" font-size="8">{format_bar_value(value, metric)}</text>'
            )
        parts.append(
            f'<text x="{center:.1f}" y="{top + plot_height + 22}" text-anchor="middle" font-family="sans-serif" font-size="13">{escape(kernel)}</text>'
        )

    legend_y = height - 48
    for index, label in enumerate(DESIGN_LABELS):
        x = left + index * 235
        parts.append(
            f'<rect x="{x}" y="{legend_y}" width="12" height="12" fill="{DESIGN_SHADES[index]}" stroke="#000000" stroke-width="0.8"/>'
        )
        parts.append(
            f'<text x="{x+18}" y="{legend_y+11}" font-family="sans-serif" font-size="12">{escape(label)}</text>'
        )

    parts.append(
        f'<text x="18" y="{top + plot_height/2}" transform="rotate(-90 18 {top + plot_height/2})" text-anchor="middle" font-family="sans-serif" font-size="12">{escape(unit)}</text>'
    )
    parts.append("</svg>")
    (results_dir / filename).write_text("\n".join(parts) + "\n", encoding="utf-8")
    return True


def format_bar_value(value, metric):
    if metric == "cycles" and value >= 1000:
        return f"{value / 1000:.0f}k"
    if metric == "runtime_ms":
        return f"{value:.2g}"
    return f"{value:.2f}"


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
            ("cycles", "cycles.svg", "Simulation cycles by workload (log scale)", "Cycles (log scale, lower is better)", True),
            ("speedup", "speedup.svg", "Speedup vs. in-order baseline", "Speedup (higher is better)", False),
            ("runtime_ms", "runtime.svg", "Measured host execution time by workload (log scale)", "Host time in ms (log scale)", True),
            ("operations_per_cycle", "throughput.svg", "Operations per simulated cycle", "Operations per cycle (higher is better)", False),
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
