#include "cpu.h"
#include "decode.h"
#include <cstdio>
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
        int len = decode_instruction_length(mem, program_counter);
        // TODO: Big endian.
        instruction_buffer = *reinterpret_cast<Instruction *>(&mem.data[program_counter]);
        program_counter += len;
    }
}

void Cpu::decode() {
    Instruction inst = instruction_buffer;
    uint8_t opcode = take_bits(inst, 0, 7);
    DecodeInfo di;

    switch (opcode) {
    case OP_IMM:
        di = decode_i_type(inst);
        switch (di.funct3) {
        case F_ADDI:
            regs[di.rd] = regs[di.rs1] + sign_extend(di.imm, 12);
            fprintf(stderr, "    addi x%u x%u %u\n", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        case F_SLTI: {
            int32_t rs1 = regs[di.rs1];
            int32_t imm = sign_extend(di.imm, 12);
            if (rs1 < imm) {
                regs[di.rd] = 1;
            } else {
                regs[di.rd] = 0;
            }
            fprintf(stderr, "    slti x%u x%u %u\n", di.rd, di.rs1, sign_extend(di.imm, 12));
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
            fprintf(stderr, "    sltiu x%u x%u %u\n", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        }
        case F_ANDI:
            regs[di.rd] = regs[di.rs1] & sign_extend(di.imm, 12);
            fprintf(stderr, "    andi x%u x%u %u\n", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        case F_ORI:
            regs[di.rd] = regs[di.rs1] | sign_extend(di.imm, 12);
            fprintf(stderr, "    ori x%u x%u %u\n", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        case F_XORI:
            regs[di.rd] = regs[di.rs1] ^ sign_extend(di.imm, 12);
            fprintf(stderr, "    xori x%u x%u %u\n", di.rd, di.rs1, sign_extend(di.imm, 12));
            break;
        case F_SLLI:
            regs[di.rd] = regs[di.rs1] << di.imm;
            fprintf(stderr, "    slli x%u x%u %u\n", di.rd, di.rs1, di.imm);
            break;
        case F_SRLI: {
            int shamt = di.imm & 0b11111;
            if ((di.imm >> 5) == 0) { // F_SRLI
                regs[di.rd] = regs[di.rs1] >> shamt;
                fprintf(stderr, "    srli x%u x%u %u\n", di.rd, di.rs1, shamt);
            } else { // F_SRAI
                regs[di.rd] = static_cast<int32_t>(regs[di.rs1]) >> shamt;
                fprintf(stderr, "    srai x%u x%u %u\n", di.rd, di.rs1, shamt);
            }
            break;
        }
        default:
            fatal("decode: unrecognized funct");
            break;
        }
        break;
    case OP_LUI:
        di = decode_u_type(inst);
        regs[di.rd] = di.imm << 12;
        fprintf(stderr, "    lui x%u 0x%x\n", di.rd, di.imm);
        break;
    case OP_AUIPC:
        di = decode_u_type(inst);
        regs[di.rd] = program_counter + (di.imm << 12);
        fprintf(stderr, "    auipc x%u 0x%x\n", di.rd, di.imm);
        break;
    case OP_OP:
        di = decode_r_type(inst);
        switch (di.funct3) {
        case F_ADD: { // F_SUB
            if (di.funct7 == 0) {// F_ADD
                regs[di.rd] = regs[di.rs1] + regs[di.rs2];
                fprintf(stderr, "    add x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
            } else { // F_SUB
                regs[di.rd] = regs[di.rs1] - regs[di.rs2];
                fprintf(stderr, "    sub x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
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
            fprintf(stderr, "    slt x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
            break;
        }
        case F_SLTU: {
            uint32_t rs1 = regs[di.rs1];
            uint32_t rs2 = regs[di.rs2];
            if (rs1 < rs2)
                regs[di.rd] = 1;
            else
                regs[di.rd] = 0;
            fprintf(stderr, "    sltu x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
            break;
        }
        case F_AND:
            regs[di.rd] = regs[di.rs1] & regs[di.rs2];
            fprintf(stderr, "    and x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
            break;
        case F_OR:
            regs[di.rd] = regs[di.rs1] | regs[di.rs2];
            fprintf(stderr, "    or x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
            break;
        case F_XOR:
            regs[di.rd] = regs[di.rs1] ^ regs[di.rs2];
            fprintf(stderr, "    xor x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
            break;
        case F_SLL: {
            int shamt = regs[di.rs2] & 0b11111;
            regs[di.rd] = regs[di.rs1] << shamt;
            fprintf(stderr, "    sll x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
            break;
        }
        case F_SRL: {
            int shamt = regs[di.rs2] & 0b11111;
            if (di.funct7 == 0) { // F_SRL
                regs[di.rd] = regs[di.rs1] >> shamt;
                fprintf(stderr, "    srl x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
            } else { // F_SRA
                regs[di.rd] = static_cast<int32_t>(regs[di.rs1]) >> shamt;
                fprintf(stderr, "    sra x%u x%u x%u\n", di.rd, di.rs1, di.rs2);
            }
            break;
        }
        default:
            fatal("decode: unrecognized funct");
            break;
        }
        break;
    default:
        fatal("decode: unrecognized opcode %x", opcode);
        break;
    }
}

void Cpu::run_cycle() {
    printf("pc: 0x%lx\n", program_counter);

    // Right now, decode decodes AND also executes instructions for simplicity.
    // This has to be branched out as a separate function in the near future.
    // Also, currently this is a single-cycle implementation, NOT a pipelined
    // one; fetch and decode both handle the same instruction.
    fetch();
    decode();
    cycle++;
}

void Cpu::read_elf_header(std::ifstream &ifs) {
    // NOTE: Assumes RISCV32
    Elf32_Ehdr elf_header;
    ifs.read(reinterpret_cast<char *>(&elf_header), sizeof(elf_header));
    program_counter = elf_header.e_entry;

    Elf32_Phdr program_header;
    ifs.read(reinterpret_cast<char *>(&program_header), sizeof(program_header));
}

void Cpu::load_program(const char *path) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs) {
        fatal("failed to open file");
    }

    read_elf_header(ifs);
    mem.load_program(ifs);
}

// TODO use mmap
void Memory::load_program(std::ifstream &ifs) {
    ifs.seekg(0, std::ios::end);
    size_t filesize = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    // XXX: fixed load offset (RISC-V: 0x10000, x86-64: 0x400000)
    int offset = 0x10000;
    char *inbuf = reinterpret_cast<char *>(data.get() + offset);
    ifs.read(inbuf, filesize);
    printf("Read %ld bytes\n", filesize);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s EXEC-FILE\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    Memory mem(1024 * 1024);
    Cpu cpu(mem);

    cpu.load_program(argv[1]);

    printf("Program entry point: 0x%lx\n", cpu.program_counter);
    printf("Entry instruction: ");
    for (int i = 0; i < 4; i++) {
        printf("%02x ", mem.data[cpu.program_counter + i]);
    }
    printf("\n");

    for (;;) {
        cpu.run_cycle();
    }

    printf("Simulated %ld cycles\n", cpu.cycle);
    return 0;
}
