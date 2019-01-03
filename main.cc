#include "cpu.h"

#include <cstdio>
#include <fstream>

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
void Memory::load_program(const char *path) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary | std::ios::ate);

    ifs.seekg(0, std::ios::end);
    size_t filesize = static_cast<size_t>(ifs.tellg());
    ifs.seekg(0, std::ios::beg);

    ifs.read(reinterpret_cast<char *>(data.get()), filesize);
    // printf("Read %ld bytes\n", filesize);
}

int main() {
    Memory mem(1024 * 1024);
    Cpu cpu(&mem);

    mem.load_program("main.cc");

    while (cpu.cycle < 10000) {
        cpu.run_cycle();
    }

    printf("Simulated %ld cycles\n", cpu.cycle);
    return 0;
}
