#ifndef BIT_SRAM_ARRAY_H
#define BIT_SRAM_ARRAY_H

#include <array>
#include <cstdint>
#include <stdexcept>

class BitSRAMArray {
public:
    static constexpr int ROWS = 256;
    static constexpr int BITS_PER_ROW = 256;

private:
    using Row = std::array<uint8_t, BITS_PER_ROW>;
    std::array<Row, ROWS> memory{};

    static void checkRow(int row) {
        if (row < 0 || row >= ROWS) {
            throw std::out_of_range("Invalid SRAM row");
        }
    }

public:
    void writeBit(int row, int col, uint8_t value) {
        checkRow(row);

        if (col < 0 || col >= BITS_PER_ROW || value > 1) {
            throw std::out_of_range("Invalid bit address or value");
        }

        memory[row][col] = value;
    }

    uint8_t readBit(int row, int col) const {
        checkRow(row);

        if (col < 0 || col >= BITS_PER_ROW) {
            throw std::out_of_range("Invalid bit address");
        }

        return memory[row][col];
    }

    void activateTwoRows(int rowA, int rowB) const {
        checkRow(rowA);
        checkRow(rowB);

        if (rowA == rowB) {
            throw std::invalid_argument(
                "Two distinct rows are required");
        }
    }

    uint8_t computeAND(int rowA, int rowB, int col) const {
        activateTwoRows(rowA, rowB);

        if (col < 0 || col >= BITS_PER_ROW) {
            throw std::out_of_range("Invalid bit column");
        }

        return readBit(rowA, col) & readBit(rowB, col);
    }
};

#endif