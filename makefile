CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra

TARGET   := pipeimc_sim
SRCS     := main.cpp matmul.cpp stencil.cpp kmeans.cpp
OBJS     := $(SRCS:.cpp=.o)

# Default CLI variables (can be overridden from terminal: e.g. make run KERNEL=matmul MODE=fine_grained)
KERNEL   ?= all
MODE     ?= all
ITERS    ?= 32

.PHONY: all clean run help \
        matmul stencil kmeans knn \
        baseline pipelined renaming fine_grained

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

%.o: %.cpp types.hpp kernels.hpp rename_unit.hpp simulator.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Generic parameterized run: make run KERNEL=matmul MODE=renaming
run: $(TARGET)
	./$(TARGET) --kernel $(KERNEL) --mode $(MODE) --iters $(ITERS)

# Shortcut targets by Kernel (runs all 4 modes on the chosen kernel)
matmul: $(TARGET)
	./$(TARGET) --kernel matmul --mode $(MODE) --iters $(ITERS)

stencil: $(TARGET)
	./$(TARGET) --kernel stencil --mode $(MODE) --iters $(ITERS)

kmeans: $(TARGET)
	./$(TARGET) --kernel kmeans --mode $(MODE) --iters $(ITERS)

knn: $(TARGET)
	./$(TARGET) --kernel knn --mode $(MODE) --iters $(ITERS)

# Shortcut targets by Optimization Mode (runs chosen mode across selected kernel(s))
baseline: $(TARGET)
	./$(TARGET) --kernel $(KERNEL) --mode baseline --iters $(ITERS)

pipelined: $(TARGET)
	./$(TARGET) --kernel $(KERNEL) --mode pipelined --iters $(ITERS)

renaming: $(TARGET)
	./$(TARGET) --kernel $(KERNEL) --mode renaming --iters $(ITERS)

fine_grained: $(TARGET)
	./$(TARGET) --kernel $(KERNEL) --mode fine_grained --iters $(ITERS)

clean:
	rm -f $(OBJS) $(TARGET)

help:
	@echo "======================================================================"
	@echo " PipeIMC Simulator Makefile Commands:"
	@echo "   make                                  -> Compile pipeimc_sim"
	@echo "   make run                              -> Run all kernels & all 4 modes"
	@echo "   make run KERNEL=matmul MODE=pipelined -> Run specific kernel & mode"
	@echo "   make matmul                           -> Run matmul on all 4 modes"
	@echo "   make matmul MODE=fine_grained         -> Run matmul with fine-grained issue"
	@echo "   make stencil MODE=renaming            -> Run stencil with register renaming"
	@echo "   make kmeans MODE=baseline             -> Run kmeans on in-order baseline"
	@echo "   make knn MODE=fine_grained            -> Run knn with fine-grained issue"
	@echo "======================================================================"