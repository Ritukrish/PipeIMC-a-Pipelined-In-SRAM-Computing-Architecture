
# ============================================================
# PipeIMC Simulator Makefile
# ============================================================

CXX = g++
CXXFLAGS = -O3 -std=c++17 -Wall -Wextra

TARGET = build/pipeimc

SRC_DIR = src
RESULT_DIR = results

SOURCES = \
	$(SRC_DIR)/main.cpp \
	$(SRC_DIR)/sim_engine.cpp \
	$(SRC_DIR)/model_baseline.cpp \
	$(SRC_DIR)/model_pipelined.cpp \
	$(SRC_DIR)/model_renaming.cpp \
	$(SRC_DIR)/model_fine_grained.cpp \
	$(SRC_DIR)/trace_generator.cpp \
	$(SRC_DIR)/functional.cpp

HEADERS = \
	$(SRC_DIR)/sim_engine.hpp \
	$(SRC_DIR)/sim_op.hpp \
	$(SRC_DIR)/trace_generator.hpp \
	$(SRC_DIR)/model_baseline.hpp \
	$(SRC_DIR)/model_pipelined.hpp \
	$(SRC_DIR)/model_renaming.hpp \
	$(SRC_DIR)/model_fine_grained.hpp \
	$(SRC_DIR)/functional.hpp

ITERATIONS = 256
N = 16

# ============================================================
# Default target
# ============================================================

.PHONY: all compile matmul stencil kmeans experiments func func_matmul func_stencil func_kmeans clean help

all: experiments func

# ============================================================
# Compile
# ============================================================

compile: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

# ============================================================
# Create results directory
# ============================================================

$(RESULT_DIR):
	mkdir -p $(RESULT_DIR)

# ============================================================
# Run individual kernels
# ============================================================

matmul: $(TARGET) | $(RESULT_DIR)
	./$(TARGET) matmul $(ITERATIONS)

stencil: $(TARGET) | $(RESULT_DIR)
	./$(TARGET) stencil $(ITERATIONS)

kmeans: $(TARGET) | $(RESULT_DIR)
	./$(TARGET) kmeans $(ITERATIONS)

# ============================================================
# Run all kernels
# ============================================================

experiments: matmul stencil kmeans
	@echo ""
	@echo "========================================"
	@echo " All PipeIMC experiments completed"
	@echo " Check the results/ directory"
	@echo "========================================"

# ============================================================
# Functional + timing in ONE run: the engine spends the Table III cycles
# on each IMC operation and computes the real ALU result when they elapse
# ============================================================

func_matmul: $(TARGET) | $(RESULT_DIR)
	./$(TARGET) matmul --func --n $(N)

func_stencil: $(TARGET) | $(RESULT_DIR)
	./$(TARGET) stencil --func --n $(N)

func_kmeans: $(TARGET) | $(RESULT_DIR)
	./$(TARGET) kmeans --func --n $(N)

func: func_matmul func_stencil func_kmeans
	@echo ""
	@echo "========================================"
	@echo " All functional simulations completed"
	@echo " Check the results/ directory"
	@echo "========================================"

# ============================================================
# Clean generated files
# ============================================================

clean:
	rm -rf build
	rm -f $(RESULT_DIR)/pipeimc_*.log

# ============================================================
# Help
# ============================================================

help:
	@echo "PipeIMC Simulator Commands"
	@echo "--------------------------"
	@echo "make compile    - Compile simulator"
	@echo "make matmul     - Run MATMUL"
	@echo "make stencil    - Run STENCIL"
	@echo "make kmeans     - Run KMEANS"
	@echo "make experiments- Run all kernels (timing only)"
	@echo "make func       - Functional run of matmul, stencil, kmeans (real ALU at the end of each phase, one timeline)   (N=16)"
	@echo "make func_matmul / func_stencil / func_kmeans - one kernel"
	@echo "make clean      - Remove generated files"
	@echo "make all        - Run everything (timing + functional)"
