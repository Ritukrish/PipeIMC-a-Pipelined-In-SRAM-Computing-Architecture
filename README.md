# PipeIMC: A Pipelined In-SRAM Computing Architecture

PipeIMC is a C++17 cycle-level simulator for exploring a pipelined in-SRAM computing architecture. It models SRAM-backed computation, instruction dependencies, pipeline scheduling, register renaming, and fine-grained issue.

The simulator reports cycle counts, speedups, utilization, and output verification for representative workloads.

## Features

- Matrix multiplication, stencil, and k-means/KNN-style workloads
- In-order baseline simulation
- Pipelined scoreboard execution
- Register renaming
- Fine-grained issue with pipeline freeze
- Configurable multiply latency and MSHR capacity
- Hybrid-4/hybrid-8 layout scaling
- Built-in architectural verification tests
- SRAM, bit-SRAM, and pipeline component examples

## Repository Structure

```text
.
├── main.cpp                 # PipeIMC simulator and CLI
├── simulator.hpp            # Pipeline and scheduling model
├── types.hpp                # Operations, modes, and simulator types
├── kernels.hpp              # Kernel interfaces
├── matmul.cpp               # Matrix multiplication trace generation
├── stencil.cpp              # Stencil trace generation
├── kmeans.cpp               # K-means/KNN trace generation
├── rename_unit.hpp          # Register-renaming model
├── src/
│   ├── memory/              # SRAM and bit-SRAM models
│   └── pipeline/            # Standalone pipeline simulator
├── tests/                   # Component examples
└── makefile                 # Build and run commands
```

## Requirements

- GNU Make
- A C++17-compatible compiler, such as `g++`
- Linux, macOS, or another Unix-like environment

## Build

```bash
make
```

This creates the `pipeimc_sim` executable.

## Running the Simulator

Run all kernels using all execution modes:

```bash
make run
```

Run a specific kernel and execution mode:

```bash
make run KERNEL=matmul MODE=pipelined ITERS=32
```

The equivalent direct command is:

```bash
./pipeimc_sim --kernel matmul --mode pipelined --iters 32
```

### Supported Kernels

| Kernel | Description |
|---|---|
| `matmul` | Matrix multiplication |
| `stencil` / `stencil3d` | 3D stencil computation |
| `kmeans` | K-means-style workload |
| `knn` | KNN-style workload |
| `all` | Run all supported workloads |

### Execution Modes

| Mode | Description |
|---|---|
| `baseline` | In-order baseline |
| `pipelined` | Pipelined scoreboard |
| `renaming_fig3c` | Register renaming with port-0 memory-only behavior |
| `renaming` | Pipelined execution with register renaming |
| `fine_grained` | Fine-grained issue with freeze |

### Command-Line Options

```text
--kernel <name>       Select a kernel
--mode <name>         Select an execution mode
--iters <N>           Number of iterations (default: 256)
--mul-lat <cycles>    Multiply latency (default: 105)
--mshrs <N>           Maximum in-flight memory visits (default: 8)
--scale-layout        Enable hybrid-4/hybrid-8 layout scaling
--test                Run built-in verification tests
--help                Show command-line help
```

Examples:

```bash
./pipeimc_sim --kernel all --mode all --iters 32
./pipeimc_sim --kernel matmul --mode fine_grained --mul-lat 105
./pipeimc_sim --test
```

The simulator reports:

- Total simulated cycles
- Speedup relative to the in-order baseline
- Pipeline utilization
- SRAM output
- Comparison against the expected golden value

## Makefile Shortcuts

```bash
make matmul MODE=fine_grained
make stencil MODE=renaming
make kmeans MODE=baseline
make fine_grained KERNEL=all
make clean
make help
```

## Component Examples

The `tests/` directory contains standalone examples for:

- Standard SRAM operations
- Bit-level SRAM operations
- SRAM-based arithmetic and logic
- Pipeline simulation

These examples use the headers under `src/` and can be compiled with any C++17-compatible compiler.

## Background

This project was developed for **CS5290: Memory-Centric Computing Architecture**. It is intended for architectural experimentation and comparison of scheduling and dependency-management strategies rather than production hardware implementation.
```
