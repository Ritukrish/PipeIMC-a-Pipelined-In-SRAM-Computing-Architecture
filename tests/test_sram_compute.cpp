#include <iostream>
#include "../src/memory/sram_array.h"
#include "../src/memory/sram_compute.h"

int main() {
    SRAMArray sram;

    // Store operands in SRAM
    sram.write(0, 0, 12);
    sram.write(0, 1, 10);

    // Read operands from SRAM
    uint8_t a = sram.read(0, 0);
    uint8_t b = sram.read(0, 1);

    // Compute and store results in SRAM
    sram.write(1, 0, SRAMCompute::bitwise_and(a, b));
    sram.write(1, 1, SRAMCompute::bitwise_xor(a, b));
    sram.write(1, 2, SRAMCompute::add(a, b));

    std::cout << "AND result: "
              << +sram.read(1, 0) << '\n';
    std::cout << "XOR result: "
              << +sram.read(1, 1) << '\n';
    std::cout << "ADD result: "
              << +sram.read(1, 2) << '\n';

    return 0;
}