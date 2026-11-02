#ifndef SRAM_ARRAY_H
#define SRAM_ARRAY_H

#include <array>
#include <cstdint>
#include <stdexcept>

class SRAMArray {
private:
    static constexpr int ROWS = 256;
    static constexpr int COLS = 256;

    std::array<std::array<uint8_t, COLS>, ROWS> memory{};

public:
    void write(int row, int col, uint8_t value) {
        if (row < 0 || row >= ROWS ||
            col < 0 || col >= COLS) {
            throw std::out_of_range("Invalid SRAM address");
        }
        memory[row][col] = value;
    }

    uint8_t read(int row, int col) const {
        if (row < 0 || row >= ROWS ||
            col < 0 || col >= COLS) {
            throw std::out_of_range("Invalid SRAM address");
        }
        return memory[row][col];
    }
};

#endif