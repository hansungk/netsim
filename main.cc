#include "cpu.h"

#include <cstdio>

static void fatal(const char *msg) {
    fprintf(stderr, "fatal: %s\n", msg);
    exit(EXIT_FAILURE);
}

void Cpu::fetch() {
    if (program_counter < mem->size) {
        uint8_t byte = mem->data[program_counter];
        program_counter++;
        // printf("fetched %d from 0x%08llx\n", byte, program_counter);
    }
}

void Cpu::run_cycle() {
    fetch();
    cycle++;
}

// TODO use mmap
void Memory::load_program(std::ifstream &ifs) {
    ifs.seekg(0, std::ios::end);
    size_t filesize = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    char *inbuf = reinterpret_cast<char *>(data.get());
    ifs.read(inbuf, filesize);
    printf("Read %ld bytes\n", filesize);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s EXEC-FILE\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    Memory mem(1024 * 1024);
    Cpu cpu(&mem);

    std::ifstream ifs(argv[1], std::ios::in | std::ios::binary);
    if (!ifs) {
        fatal("failed to open file");
    }
    read_elf_header(ifs);
    // mem.load_program(ifs);

    printf("Simulated %ld cycles\n", cpu.cycle);
    return 0;
}
