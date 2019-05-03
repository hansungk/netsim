// -*- C++ -*-
#ifndef CPU_H
#define CPU_H

#include "memory.h"
#include <fstream>
#include <cstdlib>
#include <cassert>

// Currently only supports the base RISC-V ISA that has fixed-length 32-bit
// instructions.  TODO: implement RISC-V ISA v2.2 1.2 Instruction Length
// Encoding
typedef uint32_t Instruction;

struct RegisterFile {
    uint32_t regs[32]; // Integer registers

    RegisterFile() {
        for (int i = 0; i < 32; i++) {
            // Mark registers as uninitialized
            regs[i] = 0;
        }
    }

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
    Cpu(Memory &mem): mem(mem), regs() {}
    Memory &mem;
    RegisterFile regs;
    MemAddr program_counter = 0;
    MemAddr next_program_counter = 0;
    long n_cycle = 0;

    // Fetch-Decode instruction buffer.
    Instruction instruction_buffer;

    void read_elf_header(std::ifstream &ifs);
    // Load an ELF program at `path` into memory and initialize architectural
    // states for execution.
    void load_program(const char *path);
    // Dump out register and PC values in a readable format.
    void dump_regs();

    void fetch();
    void decode();
    void cycle();
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
