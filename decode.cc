#include "cpu.h"

int decode_length(Memory &mem, MemAddr program_counter) {
    // FIXME: assumes little endian.  Since the length of the instruction is
    // encoded at the lowest-addressed byte, we only need to examine a single
    // byte right at the `program_counter` under little endian.
    uint8_t lowest = mem.data[program_counter];
    
    if ((lowest & 0b11) != 0b11) {
        return 2;
    } else if ((lowest & 0b11111) != 0b11111) {
        return 4;
    } else if ((lowest & 0b111111) != 0b111111) {
        return 6;
    } else if ((lowest & 0b1111111) != 0b1111111) {
        return 8;
    } else {
        // TODO: >64-bit instructions not implemented
        fatal("Decoding for >64b instructions is not implemented.");
    }
    return -1;
}
