#include "memory.h"

/// TODO: fetchN() assumes little endianness of the memory system. Maybe improve
/// these in the future to be endian-agnostic.

/// TODO: profile to see if inline is worthy.

uint32_t Memory::fetch32(MemAddr addr) const {
    return *reinterpret_cast<uint32_t *>(&data[addr]);
}

uint16_t Memory::fetch16(MemAddr addr) const {
    return *reinterpret_cast<uint16_t *>(&data[addr]);
}

uint8_t Memory::fetch8(MemAddr addr) const {
    return *reinterpret_cast<uint8_t *>(&data[addr]);
}

void Memory::store32(MemAddr addr, uint32_t value) {
    *reinterpret_cast<uint32_t *>(&data[addr]) = value;
}

void Memory::store16(MemAddr addr, uint16_t value) {
    *reinterpret_cast<uint16_t *>(&data[addr]) = value;
}

void Memory::store8(MemAddr addr, uint8_t value) {
    *reinterpret_cast<uint8_t *>(&data[addr]) = value;
}
