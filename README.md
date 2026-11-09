# PipeIMC Simulator

A small C++17 cycle-level simulator for comparing four execution models:

1. In-order baseline
2. Pipelined
3. Pipelined with register renaming
4. Fine-grained issue

The simulator runs simplified workload traces inspired by PipeIMC and reports
cycle counts, speedups, host execution time, and arithmetic correctness checks.
It is a teaching/research simulator, not a reproduction of the paper's complete
hardware or benchmark environment.

## Requirements

- GNU Make
- A C++17 compiler (the makefile defaults to `g++`)
- Python 3 for CSV and SVG reports

## Build and run

From the repository root:

```bash
make
```

By default, each workload uses 256 reduction/element iterations. Stencil and
k-means process four points/features per loop trip, so their traces contain
1024 points/features for 256 modeled trips. Matmul uses an 8x8 output tile and
256 reduction iterations; its output tile is separate from the reduction
count. The simulator currently limits the matmul output tile to 16x16.

Run one workload or regenerate reports from existing logs:

```bash
make matmul N=8 ITERATIONS=256
make stencil ITERATIONS=256
make kmeans ITERATIONS=256
make matvec SAMPLE_SIZE=256
make backprop SAMPLE_SIZE=256
make analyze
```

The make targets use summary-only cycle logs to avoid producing very large
per-event traces. To request detailed events for a smaller individual run:

```bash
make compile
./build/pipeimc matmul 8 --reduction-iterations 16
./build/pipeimc stencil 8
```

## Paper-workload samples

The paper specifies benchmark shapes including matmul 512x512, stencil3d
128x128x128, k-means 8192x16, matvec 4096x4096, and backprop with 16384
elements. `make paper-sample` runs bounded samples, not those full workloads:

- Matmul: 16x16 output tile with 256 reduction iterations.
- Stencil3d: 1024 sampled interior points from the 128-cubed volume.
- K-means: 16 sampled points with 1024 features each.
- Matvec: 256x256 matrix tile.
- Backprop: 256 independent elements using a simplified one-layer update.

Paper dimensions and actual sample dimensions are recorded separately in the
summary logs and `metrics.csv`. These numbers must not be interpreted as
full-paper-workload measurements. Paper samples are written under
`results/paper_sample/`.

## Reports

Runs write summary and cycle logs under `results/`. `make` also generates:

- `metrics.csv`: one row per workload and design, including cycles,
  speedup, host time, throughput, and correctness.
- `cycles.csv`, `speedup.csv`, `runtime.csv`, `throughput.csv`: chart-oriented
  tables.
- Corresponding SVG charts.

Cycle and host-runtime charts use logarithmic y-axes so workloads with
different scales remain visible together. Charts use distinct grayscale
shades with black outlines for color-blind-friendly viewing and printing.
Host runtime includes simulator and logging overhead and is machine-dependent;
use simulated cycles for architecture comparisons.

## Interpreting renaming results

Renaming removes false register-name dependencies (WAR/WAW), not true
read-after-write dependencies. The pipelined and renamed modes in this
simulator use the same execution ports, including a single calculation port.
Consequently, a workload only benefits when false dependencies prevent useful
work from overlapping a memory wait; renaming cannot create extra ALU capacity.
The current simplified kernels contain true accumulator dependencies, branch
fences, or limited independent work, so small cycle differences between these
two models are expected. Do not force the renamed model to be faster than the
measured schedule.

## Cleanup

```bash
make clean
```

This removes generated `build/` and `results/` outputs. It does not remove
source files, scripts, paper files, or archived implementations.
