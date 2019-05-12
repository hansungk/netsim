#include "decode.h"

int decode_inst_length(Memory &mem, MemAddr program_counter) {
  // Assumes little endian.  Since the length of the instruction is encoded
  // at the lowest-addressed byte, we only need to examine a single byte
  // right at the `program_counter` under little endian.
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
    // TODO: >64-bit instructions
    fatal("Decoding for >64b instructions is not implemented.");
  }
  return -1;
}

DecodeInfo decode_r_type(Instruction inst) {
  DecodeInfo di;
  di.opcode = take_bits(inst, 0, 7);
  di.rd = take_bits(inst, 7, 5);
  di.funct3 = take_bits(inst, 12, 3);
  di.rs1 = take_bits(inst, 15, 5);
  di.rs2 = take_bits(inst, 20, 5);
  di.funct7 = take_bits(inst, 25, 7);
  return di;
}

DecodeInfo decode_i_type(Instruction inst) {
  DecodeInfo di;
  di.opcode = take_bits(inst, 0, 7);
  di.rd = take_bits(inst, 7, 5);
  di.funct3 = take_bits(inst, 12, 3);
  di.rs1 = take_bits(inst, 15, 5);
  di.imm = take_bits(inst, 20, 12);
  return di;
}

DecodeInfo decode_s_type(Instruction inst) {
  DecodeInfo di;
  di.opcode = take_bits(inst, 0, 7);
  di.rd = take_bits(inst, 7, 5);
  di.funct3 = take_bits(inst, 12, 3);
  di.rs1 = take_bits(inst, 15, 5);
  di.rs2 = take_bits(inst, 20, 5);
  uint32_t imm11_5 = take_bits(inst, 25, 7);
  uint32_t imm4_0 = take_bits(inst, 7, 5);
  di.imm = (imm11_5 << 5) | imm4_0;
  return di;
}

DecodeInfo decode_b_type(Instruction inst) {
  DecodeInfo di;
  di.opcode = take_bits(inst, 0, 7);
  di.funct3 = take_bits(inst, 12, 3);
  di.rs1 = take_bits(inst, 15, 5);
  di.rs2 = take_bits(inst, 20, 5);
  uint32_t imm11 = take_bits(inst, 7, 1);
  uint32_t imm4_1 = take_bits(inst, 8, 4);
  uint32_t imm10_5 = take_bits(inst, 25, 6);
  uint32_t imm12 = take_bits(inst, 31, 1);
  di.imm = (imm12 << 12) | (imm11 << 11) | (imm10_5 << 5) | (imm4_1 << 1);
  return di;
}

DecodeInfo decode_u_type(Instruction inst) {
  DecodeInfo di;
  di.opcode = take_bits(inst, 0, 7);
  di.rd = take_bits(inst, 7, 5);
  di.imm = take_bits(inst, 12, 20);
  return di;
}

DecodeInfo decode_j_type(Instruction inst) {
  DecodeInfo di;
  di.opcode = take_bits(inst, 0, 7);
  di.rd = take_bits(inst, 7, 5);
  uint32_t imm19_12 = take_bits(inst, 12, 8);
  uint32_t imm11 = take_bits(inst, 20, 1);
  uint32_t imm10_1 = take_bits(inst, 21, 10);
  uint32_t imm20 = take_bits(inst, 31, 1);
  di.imm = (imm20 << 20) | (imm19_12 << 12) | (imm11 << 11) | (imm10_1 << 1);
  return di;
}
