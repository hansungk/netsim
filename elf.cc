#include "cpu.h"
#include <elf.h>
#include <unistd.h>

void read_elf_header(std::ifstream &ifs) {
    // NOTE: Assumes RISCV64
    Elf64_Ehdr elf_header;
    printf("Reading %zu bytes\n", sizeof(elf_header));
    ifs.read(reinterpret_cast<char *>(&elf_header), sizeof(elf_header));
    printf("%lx\n", elf_header.e_entry);

    Elf64_Phdr program_header;
    ifs.read(reinterpret_cast<char *>(&program_header), sizeof(program_header));

    printf("Section header starts at %lu\n", elf_header.e_shoff);
    printf("%d section entries\n", elf_header.e_shnum);
    printf("%d bytes section size\n", elf_header.e_shentsize);
}
