#ifndef SRAM_COMPUTE_H
#define SRAM_COMPUTE_H

#include <cstdint>

class SRAMCompute {
public:
    static uint8_t bitwise_and(uint8_t a, uint8_t b) {
        return a & b;
    }

    static uint8_t bitwise_or(uint8_t a, uint8_t b) {
        return a | b;
    }

    static uint8_t bitwise_xor(uint8_t a, uint8_t b) {
        return a ^ b;
    }

    static uint8_t bitwise_not(uint8_t a) {
        return static_cast<uint8_t>(~a);
    }

    static uint8_t add(uint8_t a, uint8_t b) {
        return static_cast<uint8_t>(a + b);
    }
};

#endif