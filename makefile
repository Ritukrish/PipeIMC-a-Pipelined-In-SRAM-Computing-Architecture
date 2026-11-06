
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
#   make              run matmul, stencil, kmeans
#   make matmul       full N x N matrix multiplication   (N=8, max 16)
#   make stencil      stencil kernel                     (ITERATIONS=256)
#   make kmeans       k-means kernel                     (ITERATIONS=256)
#   make clean        remove build/ and results/
#
# Example: make matmul N=16
# ============================================================

CXX      = g++
CXXFLAGS = -O3 -std=c++17 -Wall -Wextra

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

.PHONY: all compile matmul stencil kmeans clean help

all: matmul stencil kmeans
	@echo ""
	@echo "========================================"
	@echo " All simulations completed"
	@echo " Check the results/ directory"
	@echo "========================================"

compile: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

matmul: $(TARGET)
	./$(TARGET) matmul $(N)

stencil: $(TARGET)
	./$(TARGET) stencil $(ITERATIONS)

kmeans: $(TARGET)
	./$(TARGET) kmeans $(ITERATIONS)

clean:
	rm -rf build $(RESULT_DIR)

help:
	@sed -n '3,10p' makefile