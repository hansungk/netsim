#include "cpu.h"
#include "decode.h"
#include "sim.h"

namespace {
void dump_i_type(const char *op, uint32_t rd, uint32_t rs1, uint32_t imm) {
    printf("%s %s,%s,%d\n", op, RegFile::get_name(rd), RegFile::get_name(rs1),
           imm);
}

void dump_u_type(const char *op, uint32_t rd, uint32_t imm) {
    printf("%s %s,0x%x\n", op, RegFile::get_name(rd), imm);
}

void dump_r_type(const char *op, uint32_t rd, uint32_t rs1, uint32_t rs2) {
    printf("%s %s,%s,%s\n", op, RegFile::get_name(rd), RegFile::get_name(rs1),
           RegFile::get_name(rs2));
}

void dump_b_type(const char *op, uint32_t rs1, uint32_t rs2, uint32_t pc) {
    printf("%s %s,%s,0x%x\n", op, RegFile::get_name(rs1),
           RegFile::get_name(rs2), pc);
}

void dump_mem_type(const char *op, uint32_t rd_rs2, uint32_t rs1,
                   uint32_t imm) {
    printf("%s %s,%d(%s)\n", op, RegFile::get_name(rd_rs2), imm,
           RegFile::get_name(rs1));
}
} // namespace

void dump_regs(const RegFile &regs) {
    for (int i = 0; i < 32; i++) {
        printf("%3s: %#10x %9d ", RegFile::get_name(i), regs[i], regs[i]);
        if ((i + 1) % 4 == 0)
            printf("\n");
    }
    printf("\n");
}

void Cpu::fetch() {
  pc = pc_next;
  instruction_buffer = mmu.read32(pc);
}

void Cpu::decode_and_execute() {
    Instruction inst = instruction_buffer;
    MemAddr target_pc = ~0;
    uint8_t opcode = take_bits(inst, 0, 7);
    DecodeInfo di;

    // fprintf(stderr, "pc: 0x%x, inst: %08x\n", pc, inst);

    // Default nextPC = PC + 4
    // int len = decode_inst_length(mem, pc);
    int len = 4; // FIXME
    pc_next = pc + len;

    regs[zero] = 0;

    switch (opcode) {
    case OP_IMM:
        di = decode_i_type(inst);
        switch (di.funct3) {
        case F_ADDI:
            regs[di.rd] = regs[di.rs1] + sign_extend(di.imm, 12);
            dump_i_type("addi", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        case F_SLTI: {
            int32_t rs1 = regs[di.rs1];
            int32_t imm = sign_extend(di.imm, 12);
            if (rs1 < imm) {
                regs[di.rd] = 1;
            } else {
                regs[di.rd] = 0;
            }
            dump_i_type("slti", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        }
        case F_SLTIU: {
            uint32_t rs1 = regs[di.rs1];
            uint32_t imm = sign_extend(di.imm, 12);
            if (rs1 < imm) {
                regs[di.rd] = 1;
            } else {
                regs[di.rd] = 0;
            }
            dump_i_type("sltiu", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        }
        case F_ANDI:
            regs[di.rd] = regs[di.rs1] & sign_extend(di.imm, 12);
            dump_i_type("andi", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        case F_ORI:
            regs[di.rd] = regs[di.rs1] | sign_extend(di.imm, 12);
            dump_i_type("ori", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        case F_XORI:
            regs[di.rd] = regs[di.rs1] ^ sign_extend(di.imm, 12);
            dump_i_type("xori", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        case F_SLLI:
            regs[di.rd] = regs[di.rs1] << di.imm;
            dump_i_type("slli", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        case F_SRLI: {
            int shamt = di.imm & 0b11111;
            if ((di.imm >> 5) == 0) { // F_SRLI
                regs[di.rd] = regs[di.rs1] >> shamt;
                dump_i_type("srli", di.rd, di.rs1, shamt);
            } else { // F_SRAI
                regs[di.rd] = static_cast<int32_t>(regs[di.rs1]) >> shamt;
                dump_i_type("srai", di.rd, di.rs1, shamt);
            }
            break;
        }
        default:
            fatal("decode: unrecognized funct for OP_IMM");
            break;
        }
        break;
    case OP_LUI:
        di = decode_u_type(inst);
        regs[di.rd] = di.imm << 12;
        dump_u_type("lui", di.rd, di.imm);
        break;
    case OP_AUIPC:
        di = decode_u_type(inst);
        regs[di.rd] = pc + (di.imm << 12);
        dump_u_type("auipc", di.rd, di.imm);
        break;
    case OP_OP:
        di = decode_r_type(inst);
        switch (di.funct3) {
        case F_ADD: {             // F_SUB
            if (di.funct7 == 0) { // F_ADD
                regs[di.rd] = regs[di.rs1] + regs[di.rs2];
                dump_r_type("add", di.rd, di.rs1, di.rs2);
            } else { // F_SUB
                regs[di.rd] = regs[di.rs1] - regs[di.rs2];
                dump_r_type("sub", di.rd, di.rs1, di.rs2);
            }
            break;
        }
        case F_SLT: {
            int32_t rs1 = regs[di.rs1];
            int32_t rs2 = regs[di.rs2];
            if (rs1 < rs2)
                regs[di.rd] = 1;
            else
                regs[di.rd] = 0;
            dump_r_type("slt", di.rd, di.rs1, di.rs2);
            break;
        }
        case F_SLTU: {
            uint32_t rs1 = regs[di.rs1];
            uint32_t rs2 = regs[di.rs2];
            if (rs1 < rs2)
                regs[di.rd] = 1;
            else
                regs[di.rd] = 0;
            dump_r_type("sltu", di.rd, di.rs1, di.rs2);
            break;
        }
        case F_AND:
            regs[di.rd] = regs[di.rs1] & regs[di.rs2];
            dump_r_type("and", di.rd, di.rs1, di.rs2);
            break;
        case F_OR:
            regs[di.rd] = regs[di.rs1] | regs[di.rs2];
            dump_r_type("or", di.rd, di.rs1, di.rs2);
            break;
        case F_XOR:
            regs[di.rd] = regs[di.rs1] ^ regs[di.rs2];
            dump_r_type("xor", di.rd, di.rs1, di.rs2);
            break;
        case F_SLL: {
            int shamt = regs[di.rs2] & 0b11111;
            regs[di.rd] = regs[di.rs1] << shamt;
            dump_r_type("sll", di.rd, di.rs1, di.rs2);
            break;
        }
        case F_SRL: {
            int shamt = regs[di.rs2] & 0b11111;
            if (di.funct7 == 0) { // F_SRL
                regs[di.rd] = regs[di.rs1] >> shamt;
                dump_r_type("srl", di.rd, di.rs1, di.rs2);
            } else { // F_SRA
                regs[di.rd] = static_cast<int32_t>(regs[di.rs1]) >> shamt;
                dump_r_type("sra", di.rd, di.rs1, di.rs2);
            }
            break;
        }
        default:
            fatal("decode: unrecognized funct for OP");
            break;
        }
        break;
    case OP_JAL:
        di = decode_j_type(inst);
        pc_next = pc + sign_extend(di.imm, 20);
        regs[di.rd] = pc + len;
        printf("jal %s,0x%x\n", RegFile::get_name(di.rd), pc_next);
        break;
    case OP_JALR:
        di = decode_i_type(inst);
        // FIXME di.imm sign extend?
        pc_next = regs[di.rs1] + sign_extend(di.imm, 12);
        pc_next = pc_next >> 1 << 1;
        regs[di.rd] = pc + len;
        // TODO dump all the variants, e.g. jr and ret
        printf("jalr %s,%s,%+d\n", RegFile::get_name(di.rd),
               RegFile::get_name(di.rs1), sign_extend(di.imm, 12));
        break;
    case OP_BRANCH:
        di = decode_b_type(inst);
        target_pc = pc + sign_extend(di.imm, 12);
        switch (di.funct3) {
        case F_BEQ:
            if (regs[di.rs1] == regs[di.rs2])
                pc_next = target_pc;
            dump_b_type("beq", di.rs1, di.rs2, target_pc);
            break;
        case F_BNE:
            if (regs[di.rs1] != regs[di.rs2])
                pc_next = target_pc;
            dump_b_type("bne", di.rs1, di.rs2, target_pc);
            break;
        case F_BLT:
            if (static_cast<int32_t>(regs[di.rs1]) <
                static_cast<int32_t>(regs[di.rs2]))
                pc_next = target_pc;
            dump_b_type("blt", di.rs1, di.rs2, target_pc);
            break;
        case F_BLTU:
            if (regs[di.rs1] < regs[di.rs2])
                pc_next = target_pc;
            dump_b_type("bltu", di.rs1, di.rs2, target_pc);
            break;
        case F_BGE:
            if (static_cast<int32_t>(regs[di.rs1]) >=
                static_cast<int32_t>(regs[di.rs2]))
                pc_next = target_pc;
            dump_b_type("bge", di.rs1, di.rs2, target_pc);
            break;
        case F_BGEU:
            if (regs[di.rs1] >= regs[di.rs2])
                pc_next = target_pc;
            dump_b_type("bgeu", di.rs1, di.rs2, target_pc);
            break;
        default:
            fatal("decode: unrecognized funct for BRANCH");
            break;
        }
        break;
    case OP_LOAD: {
        di = decode_i_type(inst);
        MemAddr addr = regs[di.rs1] + sign_extend(di.imm, 12);

        switch (di.funct3) {
        case F_LB:
            regs[di.rd] = sign_extend(mmu.read8(addr), 8);
            dump_mem_type("lb", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        case F_LBU:
            regs[di.rd] = static_cast<uint32_t>(mmu.read8(addr));
            dump_mem_type("lbu", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        case F_LH:
            regs[di.rd] = sign_extend(mmu.read16(addr), 16);
            dump_mem_type("lh", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        case F_LHU:
            regs[di.rd] = static_cast<uint32_t>(mmu.read16(addr));
            dump_mem_type("lhu", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        case F_LW:
            regs[di.rd] = mmu.read32(addr);
            dump_mem_type("lw", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        default:
            fatal("decode: unrecognized funct for LOAD");
            break;
        }
        break;
    }
    case OP_STORE: {
        di = decode_s_type(inst);
        MemAddr addr = regs[di.rs1] + sign_extend(di.imm, 12);

        switch (di.funct3) {
        case F_SB:
            dump_mem_type("sb", di.rs2, di.rs1, sign_extend(di.imm, 12));
            printf("storing %u (0x%x) to *0x%x\n", regs[di.rs2], regs[di.rs2],
                   addr);
            mmu.write8(addr, regs[di.rs2]);
            break;
        case F_SH:
            dump_mem_type("sh", di.rs2, di.rs1, sign_extend(di.imm, 12));
            printf("storing %u (0x%x) to *0x%x\n", regs[di.rs2], regs[di.rs2],
                   addr);
            mmu.write16(addr, regs[di.rs2]);
            break;
        case F_SW:
            dump_mem_type("sw", di.rs2, di.rs1, sign_extend(di.imm, 12));
            printf("storing %u (0x%x) to *0x%x\n", regs[di.rs2], regs[di.rs2],
                   addr);
            mmu.write32(addr, regs[di.rs2]);
            break;
        default:
            fatal("decode: unrecognized funct for STORE");
            break;
        }
        break;
    }
    case OP_SYSTEM: {
        di = decode_i_type(inst);

        printf("ecall\n");

        switch (di.funct3) {
        case F_PRIV:
            if (regs[a0] == 93) {
                printf("return code was %d\n", regs[a0]);
                exit(0); // FIXME
            } else {
                mmu.page_table.print();
                fatal("decode: unimplemented ECALL: %d", regs[a7]);
            }
            break;
        }
        break;
    }
    default:
        fatal("decode: unrecognized opcode %x", opcode);
        break;
    }
}

void Cpu::cycle() {
    // Right now, decode decodes *and* also executes instructions.  This has to
    // be branched out as a separate function in the future.  Also, currently
    // this is a single-cycle implementation, not a pipelined one; i.e. fetch
    // and decode handle the same instruction.
    fetch();
    decode_and_execute();
    printf("pc: 0x%x\n", pc);
    dump_regs(regs);
    n_cycle++;
}
