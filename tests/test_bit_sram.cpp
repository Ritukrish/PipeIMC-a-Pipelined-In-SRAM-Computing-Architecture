#include <iostream>
#include "../src/memory/bit_sram_array.h"

int main() {
    BitSRAMArray sram;

    sram.writeBit(10, 0, 1);
    sram.writeBit(20, 0, 1);

    sram.writeBit(10, 1, 1);
    sram.writeBit(20, 1, 0);

    std::cout << "AND bit 0: "
              << +sram.computeAND(10, 20, 0) << '\n';

    std::cout << "AND bit 1: "
              << +sram.computeAND(10, 20, 1) << '\n';

    return 0;
}