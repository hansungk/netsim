#ifndef DECODE_H
#define DECODE_H

#include "cpu.h"

#define OP_IMM  0b0010011
#define F_ADDI  0b000
#define F_SLTI  0b010
#define F_SLTIU 0b011
#define F_XORI  0b100
#define F_ORI   0b110
#define F_ANDI  0b111

struct DecodeInfo_RType {
    uint32_t funct7;
    uint32_t rs2;
    uint32_t rs1;
    uint32_t funct3;
    uint32_t rd;
    uint32_t opcode;
};

struct DecodeInfo_IType {
    uint32_t imm;
    uint32_t rs1;
    uint32_t funct3;
    uint32_t rd;
    uint32_t opcode;
};

inline uint32_t sign_extend(uint32_t value, int len) {
    return value << (32 - len) >> (32 - len);
}

inline uint32_t take_bits(Instruction inst, int pos, int len) {
    Instruction mask = ~((~0) << len) << pos;
    return static_cast<uint8_t>((inst & mask) >> pos);
}

DecodeInfo_RType decode_r_type(Instruction inst);
DecodeInfo_IType decode_i_type(Instruction inst);
// Decode length of the instruction that starts at mem.data[program_counter].
int decode_instruction_length(Memory &mem, MemAddr program_counter);

#endif
