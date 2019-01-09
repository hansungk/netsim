// -*- C++ -*-
#ifndef CPU_H
#define CPU_H

#include <fstream>
#include <memory>
#include <cstdlib>
#include <cassert>

struct Memory {
    Memory(uint64_t size): size(size) {
        data = std::make_unique<uint8_t[]>(size);
    }
    uint64_t size;
    std::unique_ptr<uint8_t[]> data;

    void load_program(std::ifstream &ifs);
};

typedef uint64_t MemAddr;

// Currently only supports the base RISC-V ISA that has fixed-length 32-bit
// instructions.  TODO: implement RISC-V ISA v2.2 1.2 Instruction Length
// Encoding
typedef uint32_t Instruction;

struct RegisterFile {
    uint32_t regs[32]; // Integer registers

    uint32_t& operator[](int index) {
        return regs[index];
    }
};

// Programmer visible states for each hardware thread.
struct Context {
    RegisterFile regs;
    MemAddr program_counter = 0;
};

struct Cpu {
    Cpu(Memory &mem): mem(mem) {}
    Memory &mem;
    RegisterFile regs;
    MemAddr program_counter = 0;
    long cycle = 0;

    // Fetch-Decode instruction buffer.
    Instruction instruction_buffer;

    void read_elf_header(std::ifstream &ifs);
    // Load an ELF program at `path` into memory and initialize architectural
    // states for execution.
    void load_program(const char *path);

    void fetch();
    void decode();
    void run_cycle();
};

/* TODO */
struct FetchBuffer {
    int head;
    int tail;
    struct FetchBufferEntry {
    } *entry;
};

void fatal(const char *fmt, ...);

#endif
