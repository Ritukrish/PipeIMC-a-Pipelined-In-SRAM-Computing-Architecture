#include <iostream>
#include "../src/pipeline/pipeline_simulator.h"

int main() {
    PipelineSimulator simulator;

    std::vector<Operation> operations = {
        {1, 2, 4},
        {2, 2, 3},
        {3, 2, 2}
    };

    int cycles = simulator.run(operations);

    std::cout << "Operations: " << operations.size() << '\n';
    std::cout << "Total cycles: " << cycles << '\n';

    return 0;
}