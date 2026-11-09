
# # ============================================================
# # PipeIMC Simulator Makefile
# # ============================================================

# CXX = g++
# CXXFLAGS = -O3 -std=c++17 -Wall -Wextra

# TARGET = build/pipeimc

# SRC_DIR = src
# RESULT_DIR = results

# SOURCES = \
# 	$(SRC_DIR)/main.cpp \
# 	$(SRC_DIR)/sim_engine.cpp \
# 	$(SRC_DIR)/model_baseline.cpp \
# 	$(SRC_DIR)/model_pipelined.cpp \
# 	$(SRC_DIR)/model_renaming.cpp \
# 	$(SRC_DIR)/model_fine_grained.cpp \
# 	$(SRC_DIR)/trace_generator.cpp

# HEADERS = \
# 	$(SRC_DIR)/sim_engine.hpp \
# 	$(SRC_DIR)/sim_op.hpp \
# 	$(SRC_DIR)/trace_generator.hpp \
# 	$(SRC_DIR)/model_baseline.hpp \
# 	$(SRC_DIR)/model_pipelined.hpp \
# 	$(SRC_DIR)/model_renaming.hpp \
# 	$(SRC_DIR)/model_fine_grained.hpp \
# 	$(SRC_DIR)/kernel_data.hpp

# ITERATIONS = 256

# # ============================================================
# # Default target
# # ============================================================

# .PHONY: all compile matmul stencil kmeans experiments clean help

# all: experiments

# # ============================================================
# # Compile
# # ============================================================

# compile: $(TARGET)

# $(TARGET): $(SOURCES) $(HEADERS)
# 	@mkdir -p build
# 	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

# # ============================================================
# # Create results directory
# # ============================================================

# $(RESULT_DIR):
# 	mkdir -p $(RESULT_DIR)

# # ============================================================
# # Run individual kernels
# # ============================================================

# matmul: $(TARGET) | $(RESULT_DIR)
# 	./$(TARGET) matmul $(ITERATIONS)

# stencil: $(TARGET) | $(RESULT_DIR)
# 	./$(TARGET) stencil $(ITERATIONS)

# kmeans: $(TARGET) | $(RESULT_DIR)
# 	./$(TARGET) kmeans $(ITERATIONS)

# # ============================================================
# # Run all kernels
# # ============================================================

# experiments: matmul stencil kmeans
# 	@echo ""
# 	@echo "========================================"
# 	@echo " All PipeIMC experiments completed"
# 	@echo " Check the results/ directory (cycle logs include input values, computed values and final results)"
# 	@echo "========================================"

# # ============================================================
# # Clean generated files
# # ============================================================

# clean:
# 	rm -rf build
# 	rm -f $(RESULT_DIR)/pipeimc_*.log

# # ============================================================
# # Help
# # ============================================================

# help:
# 	@echo "PipeIMC Simulator Commands"
# 	@echo "--------------------------"
# 	@echo "make compile    - Compile simulator"
# 	@echo "make matmul     - Run MATMUL"
# 	@echo "make stencil    - Run STENCIL"
# 	@echo "make kmeans     - Run KMEANS"
# 	@echo "make experiments- Run all kernels (timing + real computation + result check)"
# 	@echo "make clean      - Remove generated files"
# 	@echo "make all        - Same as make experiments"
# ============================================================
# PipeIMC Simulator Makefile - everything runs from here, results go to results/
#
#   make              run all five available workloads
#   make matvec       bounded 256x256 tile from the 4096x4096 paper workload
#   make backprop     bounded 256-element one-layer sample
#   make matmul       N x N output tile, 256 reduction iterations (N=8, max 16)
#   make stencil      256 loop trips (4 points/trip)
#   make kmeans       256 loop trips (4 features/trip)
#   make paper-sample bounded samples of paper workloads in results/paper_sample/
#   make clean        remove build/ and generated results/
#
# Example: make matmul N=16
# ============================================================

CXX      = g++
CXXFLAGS = -O3 -std=c++17 -Wall -Wextra
PYTHON   ?= python3

TARGET     = build/pipeimc
SRC_DIR    = src
RESULT_DIR = results

SOURCES = \
	$(SRC_DIR)/main.cpp \
	$(SRC_DIR)/sim_engine.cpp \
	$(SRC_DIR)/model_baseline.cpp \
	$(SRC_DIR)/model_pipelined.cpp \
	$(SRC_DIR)/model_renaming.cpp \
	$(SRC_DIR)/model_fine_grained.cpp \
	$(SRC_DIR)/trace_generator.cpp

HEADERS = $(wildcard $(SRC_DIR)/*.hpp)

N          = 8
ITERATIONS = 256
SAMPLE_SIZE = $(ITERATIONS)

.PHONY: all compile matmul stencil kmeans matvec backprop paper-sample analyze clean help

all: matmul stencil kmeans matvec backprop
	$(PYTHON) scripts/analyze_results.py
	@echo ""
	@echo "========================================"
	@echo " All simulations and reports completed"
	@echo " Check the results/ directory for logs, CSV files, and PDF graphs"
	@echo "========================================"

compile: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

matmul: $(TARGET)
	./$(TARGET) matmul $(N) --reduction-iterations $(ITERATIONS) --summary-only

stencil: $(TARGET)
	./$(TARGET) stencil $(ITERATIONS) --summary-only

kmeans: $(TARGET)
	./$(TARGET) kmeans $(ITERATIONS) --summary-only

matvec: $(TARGET)
	./$(TARGET) matvec $(SAMPLE_SIZE) --summary-only

backprop: $(TARGET)
	./$(TARGET) backprop $(SAMPLE_SIZE) --summary-only

paper-sample: $(TARGET)
	./$(TARGET) matmul --paper-sample --summary-only
	./$(TARGET) stencil --paper-sample --summary-only
	./$(TARGET) kmeans --paper-sample --summary-only
	./$(TARGET) matvec --paper-sample --summary-only
	./$(TARGET) backprop --paper-sample --summary-only
	$(PYTHON) scripts/analyze_results.py --results-dir results/paper_sample

analyze:
	$(PYTHON) scripts/analyze_results.py

clean:
	rm -rf build $(RESULT_DIR)

help:
	@echo "PipeIMC Simulator Commands"
	@echo "--------------------------"
	@echo "make              - Run all kernels and generate CSV/PDF reports"
	@echo "make matmul       - Run output tile N=$(N), reduction iterations=$(ITERATIONS)"
	@echo "make stencil      - Run $(ITERATIONS) loop trips (4 points each)"
	@echo "make kmeans       - Run $(ITERATIONS) loop trips (4 features each)"
	@echo "make matvec       - Run bounded matvec sample (dimension=$(SAMPLE_SIZE))"
	@echo "make backprop     - Run bounded backprop sample (elements=$(SAMPLE_SIZE))"
	@echo "make paper-sample - Run bounded samples of all five workloads"
	@echo "                    matmul 512x512, stencil3d 128^3, k-means 8192x16"
	@echo "                    matvec 4096x4096, backprop 16384 elements"
	@echo "make analyze      - Generate CSV/PDF reports from existing logs"
	@echo "make clean        - Remove build/ and results/"