// -*- C++ -*-
#ifndef CPU_H
#define CPU_H

#include "event.h"
#include "memory.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>

// Currently only supports the base RISC-V ISA that has fixed-length 32-bit
// instructions.  TODO: implement RISC-V ISA v2.2 1.2 Instruction Length
// Encoding
using Instruction = uint32_t;

static const char *register_names[]{
    "0",  "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "fp", "s1", "a0",
    "a1", "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
    "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6",
};

enum RegNames {
  zero = 0,
  ra,
  sp,
  gp,
  tp,
  t0,
  t1, t2,
  fp,
  s1,
  a0, a1,
  a2, a3, a4, a5, a6, a7,
  s2, s3, s4, s5, s6, s7, s8, s9, s10, s11,
  t3, t4, t5, t6
};

/// Register file.
class RegFile {
public:
  RegFile() {
    // Mark registers as uninitialized
    std::memset(regs, 0, sizeof(regs));
  }

  uint32_t &operator[](int index) { return regs[index]; }
  const uint32_t &operator[](int index) const { return regs[index]; }
  static const char *get_name(int index) { return register_names[index]; }

private:
  uint32_t regs[32]; // Integer registers
};

/// User visible states for each hardware thread.
struct Context {
  RegFile regs;
  MemAddr program_counter = 0;
};

// Dump out register and PC values in a readable format.
void dump_regs(const RegFile &reg);

class Cpu {
public:
    Cpu(EventQueue &eq, Memory &mem) : mmu(mem), regs(), eventq(eq) {}

    // Load an ELF program at `path` into memory and initialize architectural
    // states for execution.
    void load_program(const char *path);
    void cycle();

    long n_cycle = 0;

    void set_npc(MemAddr pc) { pc_next = pc; }

    // private:
    // TODO: Discrete event simulation
    void run();

    void fetch();
    void decode_and_execute();
    void read_elf_header(std::ifstream &ifs);

    Mmu &get_mmu() { return mmu; }

    // Fetch-Decode instruction buffer.
    Instruction instruction_buffer;

    Mmu mmu;
    RegFile regs;
    MemAddr pc{0};
    MemAddr pc_next{0};

private:
    EventQueue &eventq;
};

/* TODO */
struct FetchBuffer {
  int head;
  int tail;
  struct FetchBufferEntry {
  } * entry;
};

#endif
