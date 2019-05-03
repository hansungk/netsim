#ifndef DECODE_H
#define DECODE_H

#include "cpu.h"

#define OP_LUI    0b0110111
#define OP_AUIPC  0b0010111
#define OP_JAL    0b1101111
#define OP_JALR   0b1100111
#define OP_BRANCH 0b1100011
#define OP_IMM    0b0010011
#define OP_OP     0b0110011
#define OP_LOAD   0b0000011
#define OP_STORE  0b0100011
#define OP_SYSTEM 0b1110011
#define F_BEQ   0b000
#define F_BNE   0b001
#define F_BLT   0b100
#define F_BGE   0b101
#define F_BLTU  0b110
#define F_BGEU  0b111
#define F_ADDI  0b000
#define F_SLTI  0b010
#define F_SLTIU 0b011
#define F_XORI  0b100
#define F_ORI   0b110
#define F_ANDI  0b111
#define F_SLLI  0b001
#define F_SRLI  0b101
#define F_SRAI  0b101
#define F_ADD   0b000
#define F_SUB   0b000
#define F_SLT   0b010
#define F_SLTU  0b011
#define F_XOR   0b100
#define F_OR    0b110
#define F_AND   0b111
#define F_SLL   0b001
#define F_SRL   0b101
#define F_SRA   0b101
#define F_LB    0b000
#define F_LH    0b001
#define F_LW    0b010
#define F_LBU   0b100
#define F_LHU   0b101
#define F_SB    0b000
#define F_SH    0b001
#define F_SW    0b010
#define F_PRIV  0b000

struct DecodeInfo {
    uint32_t opcode;
    uint32_t rd;
    uint32_t rs1;
    uint32_t rs2;
    uint32_t funct3;
    uint32_t funct7;
    uint32_t imm;
};

struct DecodeInfo_IType {
    uint32_t imm;
    uint32_t rs1;
    uint32_t funct3;
    uint32_t rd;
    uint32_t opcode;
};

struct DecodeInfo_UType {
    uint32_t imm;
    uint32_t rd;
    uint32_t opcode;
};

inline uint32_t sign_extend(uint32_t value, int len) {
    return static_cast<int32_t>(value) << (32 - len) >> (32 - len);
}

inline uint32_t take_bits(Instruction inst, int pos, int len) {
    Instruction mask = ~((~0u) << len);
    return (inst >> pos) & mask;
}

DecodeInfo decode_r_type(Instruction inst);
DecodeInfo decode_i_type(Instruction inst);
DecodeInfo decode_s_type(Instruction inst);
DecodeInfo decode_b_type(Instruction inst);
DecodeInfo decode_u_type(Instruction inst);
DecodeInfo decode_j_type(Instruction inst);
// Decode length of the instruction that starts at mem.data[program_counter].
int decode_inst_length(Memory &mem, MemAddr program_counter);

#endif
