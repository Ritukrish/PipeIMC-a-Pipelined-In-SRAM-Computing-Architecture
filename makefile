
# ============================================================
# PipeIMC Simulator Makefile
# ============================================================

CXX = g++
CXXFLAGS = -O3 -std=c++17 -Wall -Wextra

TARGET = pipeimc_sim.exe

SRC_DIR = src
RESULT_DIR = results

SOURCES = \
	$(SRC_DIR)/main.cpp \
	$(SRC_DIR)/sim_engine.cpp \
	$(SRC_DIR)/model_baseline.cpp \
	$(SRC_DIR)/model_pipelined.cpp \
	$(SRC_DIR)/model_renaming.cpp \
	$(SRC_DIR)/model_fine_grained.cpp \
	$(SRC_DIR)/trace_generator.cpp

HEADERS = \
	$(SRC_DIR)/sim_engine.hpp \
	$(SRC_DIR)/sim_op.hpp \
	$(SRC_DIR)/trace_generator.hpp \
	$(SRC_DIR)/model_baseline.hpp \
	$(SRC_DIR)/model_pipelined.hpp \
	$(SRC_DIR)/model_renaming.hpp \
	$(SRC_DIR)/model_fine_grained.hpp

ITERATIONS = 256

# ============================================================
# Default target
# ============================================================

.PHONY: all compile matmul stencil kmeans experiments clean help

all: experiments

# ============================================================
# Compile
# ============================================================

compile: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
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
# Clean generated files
# ============================================================

clean:
	rm -f $(TARGET)
	rm -f *.o
	rm -f $(RESULT_DIR)/pipeimc_cycle_*.log

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
	@echo "make experiments- Run all kernels"
	@echo "make clean      - Remove generated files"
	@echo "make all        - Compile and run all experiments"
