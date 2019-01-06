#include "cpu.h"
#include <cassert>

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

static uint8_t take_bits(Instruction inst, int pos, int len) {
    assert(len <= 8 && "take_bits() takes len at most 8");
    Instruction mask = ~((~0) << len) << pos;
    return static_cast<uint8_t>((inst & mask) >> pos);
}

struct DecodeInfo_RType {
    uint8_t funct7;
    uint8_t rs2;
    uint8_t rs1;
    uint8_t funct3;
    uint8_t rd;
    uint8_t opcode;
};

struct DecodeInfo_IType {
    uint16_t imm;
    uint8_t rs1;
    uint8_t funct3;
    uint8_t rd;
    uint8_t opcode;
};

static DecodeInfo_RType decode_r_type(Instruction inst) {
    DecodeInfo_RType
        di {.opcode = take_bits(inst, 0, 7),
            .rd     = take_bits(inst, 7, 5),
            .funct3 = take_bits(inst, 12, 3),
            .rs1    = take_bits(inst, 15, 5),
            .rs2    = take_bits(inst, 20, 5),
            .funct7 = take_bits(inst, 25, 7)};
    return di;
}

static DecodeInfo_IType decode_i_type(Instruction inst) {
    DecodeInfo_IType
        di {.opcode = take_bits(inst, 0, 7),
            .rd     = take_bits(inst, 7, 5),
            .funct3 = take_bits(inst, 12, 3),
            .rs1    = take_bits(inst, 15, 5),
            .imm    = take_bits(inst, 20, 12)};
    return di;
}

#define OP_IMM 0b0010011
#define F_ADDI 0b000
#define F_SLTI 0b010
#define F_ANDI 0b111
#define F_ORI  0b110
#define F_XORI 0b100

void decode_inst(Instruction inst) {
    uint8_t opcode = take_bits(inst, 0, 7);

    switch (opcode) {
    case OP_IMM:
        DecodeInfo_IType di = decode_i_type(inst);
        switch (di.funct3) {
        case F_ADDI:
        case F_SLTI:
            break;
        case F_ANDI:
        case F_ORI:
        case F_XORI:
            break;
        default:
            fatal("decode: unrecognized funct");
        }
        break;
    default:
        fatal("decode: unrecognized opcode");
    }
}
