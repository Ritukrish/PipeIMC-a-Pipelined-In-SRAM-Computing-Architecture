#include <iostream>
#include "../src/memory/sram_array.h"

int main() {
    SRAMArray sram;

    sram.write(0, 0, 10);
    sram.write(10, 20, 42);
    sram.write(255, 255, 99);

    std::cout << "Value at (0,0): "
              << static_cast<int>(sram.read(0, 0)) << '\n';

    std::cout << "Value at (10,20): "
              << static_cast<int>(sram.read(10, 20)) << '\n';

    std::cout << "Value at (255,255): "
              << static_cast<int>(sram.read(255, 255)) << '\n';

    return 0;
}