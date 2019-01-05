#include "cpu.h"
#include <cstdio>
#include <elf.h>

void fatal(const char *msg) {
    fprintf(stderr, "fatal: %s\n", msg);
    exit(EXIT_FAILURE);
}

void Cpu::fetch() {
    if (program_counter < mem.size) {
        program_counter += decode_length(mem, program_counter);
    }
}

void Cpu::run_cycle() {
    printf("pc: %lx\n", program_counter);
    fetch();
    cycle++;
}

void Cpu::read_elf_header(std::ifstream &ifs) {
    // NOTE: Assumes RISCV64
    Elf64_Ehdr elf_header;
    printf("Reading %zu bytes\n", sizeof(elf_header));
    ifs.read(reinterpret_cast<char *>(&elf_header), sizeof(elf_header));
    program_counter = elf_header.e_entry;

    Elf64_Phdr program_header;
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
    for (int i = 0; i < 4; i++) {
        printf("%02x ", mem.data[cpu.program_counter + i]);
    }
    printf("\n");

    for (int i = 0; i < 10; i++) {
        cpu.run_cycle();
    }

    printf("Simulated %ld cycles\n", cpu.cycle);
    return 0;
}
