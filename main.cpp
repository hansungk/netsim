#include "cpu.h"
#include "decode.h"
#include <vector>
#include <cstdio>
#include <cstdarg>
#include <elf.h>

void fatal(const char *fmt, ...) {
    va_list ap;

    fprintf(stderr, "fatal: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");

    exit(EXIT_FAILURE);
}

void Cpu::fetch() {
    if (program_counter < mem.size) {
        program_counter = next_program_counter;
        // TODO: Big endian.
        instruction_buffer = mem.fetch32(program_counter);
    }
}

void Cpu::decode() {
    Instruction inst = instruction_buffer;
    MemAddr target_program_counter = ~0;
    uint8_t opcode = take_bits(inst, 0, 7);
    DecodeInfo di;

    // fprintf(stderr, "pc: 0x%x, inst: %08x\n", program_counter, inst);

    // Default nextPC = PC + 4
    int len = decode_instruction_length(mem, program_counter);
    next_program_counter = program_counter + len;

    regs[0] = 0;

    switch (opcode) {
    case OP_IMM:
        di = decode_i_type(inst);
        switch (di.funct3) {
        case F_ADDI:
            regs[di.rd] = regs[di.rs1] + sign_extend(di.imm, 12);
            printf("    addi x%u x%u %d\n", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        case F_SLTI: {
            int32_t rs1 = regs[di.rs1];
            int32_t imm = sign_extend(di.imm, 12);
            if (rs1 < imm) {
                regs[di.rd] = 1;
            } else {
                regs[di.rd] = 0;
            }
            printf("    slti x%u x%u %d\n", di.rd, di.rs1, sign_extend(di.imm, 12));
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
            printf("    sltiu x%u x%u %d\n", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        }
        case F_ANDI:
            regs[di.rd] = regs[di.rs1] & sign_extend(di.imm, 12);
            printf("    andi x%u x%u %d\n", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        case F_ORI:
            regs[di.rd] = regs[di.rs1] | sign_extend(di.imm, 12);
            printf("    ori x%u x%u %d\n", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        case F_XORI:
            regs[di.rd] = regs[di.rs1] ^ sign_extend(di.imm, 12);
            printf("    xori x%u x%u %d\n", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        case F_SLLI:
            regs[di.rd] = regs[di.rs1] << di.imm;
            printf("    slli x%u x%u %d\n", di.rd, di.rs1, di.imm);
            break;
        case F_SRLI: {
            int shamt = di.imm & 0b11111;
            if ((di.imm >> 5) == 0) { // F_SRLI
                regs[di.rd] = regs[di.rs1] >> shamt;
                printf("    srli x%u x%u %d\n", di.rd, di.rs1, shamt);
            } else { // F_SRAI
                regs[di.rd] = static_cast<int32_t>(regs[di.rs1]) >> shamt;
                printf("    srai x%u x%u %d\n", di.rd, di.rs1, shamt);
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
        printf("    lui x%u 0x%x\n", di.rd, di.imm);
        break;
    case OP_AUIPC:
        di = decode_u_type(inst);
        regs[di.rd] = program_counter + (di.imm << 12);
        printf("    auipc x%u 0x%x\n", di.rd, di.imm);
        break;
    case OP_OP:
        di = decode_r_type(inst);
        switch (di.funct3) {
        case F_ADD: { // F_SUB
            if (di.funct7 == 0) {// F_ADD
                regs[di.rd] = regs[di.rs1] + regs[di.rs2];
                printf("    add x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
            } else { // F_SUB
                regs[di.rd] = regs[di.rs1] - regs[di.rs2];
                printf("    sub x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
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
            printf("    slt x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
            break;
        }
        case F_SLTU: {
            uint32_t rs1 = regs[di.rs1];
            uint32_t rs2 = regs[di.rs2];
            if (rs1 < rs2)
                regs[di.rd] = 1;
            else
                regs[di.rd] = 0;
            printf("    sltu x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
            break;
        }
        case F_AND:
            regs[di.rd] = regs[di.rs1] & regs[di.rs2];
            printf("    and x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
            break;
        case F_OR:
            regs[di.rd] = regs[di.rs1] | regs[di.rs2];
            printf("    or x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
            break;
        case F_XOR:
            regs[di.rd] = regs[di.rs1] ^ regs[di.rs2];
            printf("    xor x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
            break;
        case F_SLL: {
            int shamt = regs[di.rs2] & 0b11111;
            regs[di.rd] = regs[di.rs1] << shamt;
            printf("    sll x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
            break;
        }
        case F_SRL: {
            int shamt = regs[di.rs2] & 0b11111;
            if (di.funct7 == 0) { // F_SRL
                regs[di.rd] = regs[di.rs1] >> shamt;
                printf("    srl x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
            } else { // F_SRA
                regs[di.rd] = static_cast<int32_t>(regs[di.rs1]) >> shamt;
                printf("    sra x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
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
        next_program_counter = program_counter + sign_extend(di.imm, 20);
        regs[di.rd] = program_counter + len;
        printf("    jal x%u %x\n", di.rd, next_program_counter);
        break;
    case OP_JALR:
        di = decode_i_type(inst);
        // FIXME di.imm sign extend?
        next_program_counter = regs[di.rs1] + sign_extend(di.imm, 12);
        next_program_counter = next_program_counter >> 1 << 1;
        regs[di.rd] = program_counter + len;
        printf("    jalr x%u x%u %+d\n", di.rd, di.rs1, sign_extend(di.imm, 12));
        break;
    case OP_BRANCH:
        di = decode_b_type(inst);
        target_program_counter = program_counter + sign_extend(di.imm, 12);
        switch (di.funct3) {
        case F_BEQ:
            if (regs[di.rs1] == regs[di.rs2]) {
                next_program_counter = target_program_counter;
            }
            printf("    beq x%u x%u %x\n", di.rs1, di.rs2, target_program_counter);
            break;
        case F_BNE:
            if (regs[di.rs1] != regs[di.rs2]) {
                next_program_counter = target_program_counter;
            }
            printf("    bne x%u x%u %x\n", di.rs1, di.rs2, target_program_counter);
            break;
        case F_BLT:
            if (static_cast<int32_t>(regs[di.rs1]) < static_cast<int32_t>(regs[di.rs2])) {
                next_program_counter = target_program_counter;
            }
            printf("    blt x%u x%u %x\n", di.rs1, di.rs2, target_program_counter);
            break;
        case F_BLTU:
            if (regs[di.rs1] < regs[di.rs2]) {
                next_program_counter = target_program_counter;
            }
            printf("    bltu x%u x%u %x\n", di.rs1, di.rs2, target_program_counter);
            break;
        case F_BGE:
            if (static_cast<int32_t>(regs[di.rs1]) >= static_cast<int32_t>(regs[di.rs2])) {
                next_program_counter = target_program_counter;
            }
            printf("    bge x%u x%u %x\n", di.rs1, di.rs2, target_program_counter);
            break;
        case F_BGEU:
            if (regs[di.rs1] >= regs[di.rs2]) {
                next_program_counter = target_program_counter;
            }
            printf("    bgeu x%u x%u %x\n", di.rs1, di.rs2, target_program_counter);
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
            regs[di.rd] = sign_extend(mem.fetch8(addr), 8);
            printf("    lb x%u %d(x%u)\n", di.rd, sign_extend(di.imm, 12), di.rs1);
            break;
        case F_LBU:
            regs[di.rd] = static_cast<uint32_t>(mem.fetch8(addr));
            printf("    lbu x%u %d(x%u)\n", di.rd, sign_extend(di.imm, 12), di.rs1);
            break;
        case F_LH:
            regs[di.rd] = sign_extend(mem.fetch16(addr), 16);
            printf("    lh x%u %d(x%u)\n", di.rd, sign_extend(di.imm, 12), di.rs1);
            break;
        case F_LHU:
            regs[di.rd] = static_cast<uint32_t>(mem.fetch16(addr));
            printf("    lhu x%u %d(x%u)\n", di.rd, sign_extend(di.imm, 12), di.rs1);
            break;
        case F_LW:
            regs[di.rd] = mem.fetch32(addr);
            printf("    lw x%u %d(x%u)\n", di.rd, sign_extend(di.imm, 12), di.rs1);
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
            printf("    sb x%u %d(x%u)\n", di.rs2, sign_extend(di.imm, 12), di.rs1);
            printf("storing %u (0x%x) to *0x%x\n", regs[di.rs2], regs[di.rs2], addr);
            mem.store8(addr, regs[di.rs2]);
            break;
        case F_SH:
            printf("    sh x%u %d(x%u)\n", di.rs2, sign_extend(di.imm, 12), di.rs1);
            printf("storing %u (0x%x) to *0x%x\n", regs[di.rs2], regs[di.rs2], addr);
            mem.store16(addr, regs[di.rs2]);
            break;
        case F_SW:
            printf("    sw x%u %d(x%u)\n", di.rs2, sign_extend(di.imm, 12), di.rs1);
            printf("storing %u (0x%x) to *0x%x\n", regs[di.rs2], regs[di.rs2], addr);
            mem.store32(addr, regs[di.rs2]);
            break;
        default:
            fatal("decode: unrecognized funct for STORE");
            break;
        }
        break;
    }
    case OP_SYSTEM: {
        di = decode_i_type(inst);

        switch (di.funct3) {
        case F_PRIV:
            fatal("decode: ECALL unimplemented");
            break;
        }
        break;
    }
    default:
        fatal("decode: unrecognized opcode %x", opcode);
        break;
    }
}

void Cpu::dump_regs() {
    printf("pc: 0x%x\n", program_counter);
    for (int i = 0; i < 32; i++) {
        printf("x%2d: %#10x %9d ", i, regs[i], regs[i]);
        if ((i + 1) % 4 == 0)
            printf("\n");
    }
    printf("\n");
}

void Cpu::cycle() {
    // Right now, decode decodes *and* also executes instructions for
    // simplicity.  This has to be branched out as a separate function in the
    // future.  Also, currently this is a single-cycle implementation, not a
    // pipelined one; fetch and decode both handle the same instruction.
    fetch();
    decode();
    dump_regs();
    n_cycle++;
}

static void load_segment(Memory &mem, std::ifstream &ifs, Elf32_Phdr ph) {
    ifs.seekg(ph.p_offset, std::ios::beg);
    char *inbuf = reinterpret_cast<char *>(mem.data.get() + ph.p_vaddr);
    ifs.read(inbuf, ph.p_filesz);
    printf("Loaded segment from %x into %x, size %x\n", ph.p_offset, ph.p_vaddr, ph.p_filesz);
}

// TODO: use mmap?
void Cpu::load_program(const char *path) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs) {
        fatal("failed to open file");
    }

    // NOTE: Assumes RISCV32
    Elf32_Ehdr elf_header;
    ifs.read(reinterpret_cast<char *>(&elf_header), sizeof(elf_header));
    next_program_counter = elf_header.e_entry;
    printf("%d program headers\n", elf_header.e_phnum);

    std::vector<Elf32_Phdr> program_headers;

    for (int i = 0; i < elf_header.e_phnum; i++) {
        Elf32_Phdr ph;
        ifs.read(reinterpret_cast<char *>(&ph), sizeof(ph));
        program_headers.push_back(ph);
    }

    for (const auto ph : program_headers) {
        if (ph.p_type != PT_LOAD)
            continue;

        load_segment(mem, ifs, ph);
    }

    // Set stack pointer
    regs[2] = 0x7fffff;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s EXEC-FILE\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    Memory mem(16 * 1024 * 1024);
    Cpu cpu(mem);

    cpu.load_program(argv[1]);

    printf("Program entry point: 0x%x\n", cpu.program_counter);
    printf("Entry instruction: ");
    for (int i = 0; i < 4; i++) {
        printf("%02x ", mem.data[cpu.program_counter + i]);
    }
    printf("\n");

    for (;;) {
        cpu.cycle();
    }

    printf("Simulated %ld cycles\n", cpu.n_cycle);
    return 0;
}
