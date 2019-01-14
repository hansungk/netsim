// -*- C++ -*-
#ifndef MEMORY_H
#define MEMORY_H

#include <memory>

typedef uint64_t MemAddr;

struct Memory {
    Memory(uint64_t size): size(size) {
        data = std::make_unique<uint8_t[]>(size);
    }
    uint64_t size;
    std::unique_ptr<uint8_t[]> data;

    void load_program(std::ifstream &ifs);
    uint32_t fetch32(MemAddr addr) const;
    uint16_t fetch16(MemAddr addr) const;
    uint8_t fetch8(MemAddr addr) const;
    void store32(MemAddr addr, uint32_t value);
    void store16(MemAddr addr, uint16_t value);
    void store8(MemAddr addr, uint8_t value);
};

#endif
