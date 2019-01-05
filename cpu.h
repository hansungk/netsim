// -*- C++ -*-
#ifndef CPU_H
#define CPU_H

#include <cstdlib>
#include <fstream>
#include <memory>

struct Memory {
    Memory(uint64_t size): size(size) {
        data = std::make_unique<uint8_t[]>(size);
    }
    uint64_t size;
    std::unique_ptr<uint8_t[]> data;

    void load_program(std::ifstream &ifs);
};

typedef uint64_t MemAddr;

struct Context {
};

struct Cpu {
    Cpu(Memory &mem): mem(mem) {}
    Memory &mem;
    MemAddr program_counter = 0;
    long cycle = 0;

    void read_elf_header(std::ifstream &ifs);
    // Load an ELF program at `path` into memory and initialize architectural
    // states for execution.
    void load_program(const char *path);

    void fetch();
    void run_cycle();
};

// Currently only supports the base RISC-V ISA that has fixed-length 32-bit
// instructions.  TODO: implement RISC-V ISA v2.2 1.2 Instruction Length
// Encoding
typedef uint32_t Instruction;

/* TODO */
struct FetchBuffer {
    int head;
    int tail;
    struct FetchBufferEntry {
    } *entry;
};

// Decode length of the instruction that starts at mem.data[program_counter].
int decode_length(Memory &mem, MemAddr program_counter);
void fatal(const char *msg);

#endif
