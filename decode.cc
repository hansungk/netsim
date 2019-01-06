#include "decode.h"

int decode_instruction_length(Memory &mem, MemAddr program_counter) {
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
        // TODO: >64-bit instructions.
        fatal("Decoding for >64b instructions is not implemented.");
    }
    return -1;
}

DecodeInfo_RType decode_r_type(Instruction inst) {
    DecodeInfo_RType di;
    di.opcode = take_bits(inst, 0, 7);
    di.rd     = take_bits(inst, 7, 5);
    di.funct3 = take_bits(inst, 12, 3);
    di.rs1    = take_bits(inst, 15, 5);
    di.rs2    = take_bits(inst, 20, 5);
    di.funct7 = take_bits(inst, 25, 7);
    return di;
}

DecodeInfo_IType decode_i_type(Instruction inst) {
    DecodeInfo_IType di;
    di.opcode = take_bits(inst, 0, 7);
    di.rd     = take_bits(inst, 7, 5);
    di.funct3 = take_bits(inst, 12, 3);
    di.rs1    = take_bits(inst, 15, 5);
    di.imm    = take_bits(inst, 20, 12);
    return di;
}
