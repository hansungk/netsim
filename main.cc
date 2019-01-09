#include "cpu.h"
#include "decode.h"
#include <cstdio>
#include <elf.h>

void fatal(const char *msg) {
    fprintf(stderr, "fatal: %s\n", msg);
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
    DecodeInfo_IType di;

    switch (opcode) {
    case OP_IMM:
        di = decode_i_type(inst);
        switch (di.funct3) {
        case F_ADDI:
            regs[di.rd] = regs[di.rs1] + sign_extend(di.imm, 12);
            break;
        case F_SLTI: {
            int32_t rs1 = regs[di.rs1];
            int32_t imm = sign_extend(di.imm, 12);
            if (rs1 < imm)
                regs[di.rd] = 1;
            else
                regs[di.rd] = 0;
            break;
        }
        case F_SLTIU: {
            uint32_t rs1 = regs[di.rs1];
            uint32_t imm = sign_extend(di.imm, 12);
            if (rs1 < imm)
                regs[di.rd] = 1;
            else
                regs[di.rd] = 0;
            break;
        }
        case F_ANDI:
            regs[di.rd] = regs[di.rs1] & sign_extend(di.imm, 12);
            break;
        case F_ORI:
            regs[di.rd] = regs[di.rs1] | sign_extend(di.imm, 12);
            break;
        case F_XORI:
            regs[di.rd] = regs[di.rs1] ^ sign_extend(di.imm, 12);
            break;
        case F_SLLI:
            regs[di.rd] = regs[di.rs1] << di.imm;
            break;
        case F_SRLI: { // F_SRAI
            int shamt = di.imm & 0b11111;
            if ((di.imm >> 5) == 0)
                regs[di.rd] = regs[di.rs1] >> shamt;
            else
                regs[di.rd] = static_cast<int32_t>(regs[di.rs1]) >> shamt;
            break;
        }
        default:
            fatal("decode: unrecognized funct");
            break;
        }
        break;
    case OP_LUI:
        break;
    case OP_AUIPC:
        break;
    default:
        fatal("decode: unrecognized opcode");
        break;
    }
}

void Cpu::run_cycle() {
    printf("pc: 0x%lx\n", program_counter);
    fetch();
    decode();
    cycle++;
}

void Cpu::read_elf_header(std::ifstream &ifs) {
    // NOTE: Assumes RISCV32
    Elf32_Ehdr elf_header;
    printf("Reading %zu bytes\n", sizeof(elf_header));
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
