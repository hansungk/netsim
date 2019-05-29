#include "cpu.h"
#include "decode.h"
#include <vector>
#include <cstdio>
#include <cstdarg>
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
  program_counter = next_program_counter;
  // TODO: Big endian.
  instruction_buffer = mmu.read32(program_counter);
}

namespace {
void dump_i_type(const char *op, uint32_t rd, uint32_t rs1, uint32_t imm) {
    printf("%s %s,%s,%d\n", op, RegFile::get_name(rd), RegFile::get_name(rs1),
           imm);
}

void dump_u_type(const char *op, uint32_t rd, uint32_t imm) {
    printf("%s %s,0x%x\n", op, RegFile::get_name(rd), imm);
}

void dump_r_type(const char *op, uint32_t rd, uint32_t rs1, uint32_t rs2) {
    printf("%s %s,%s,%s\n", op, RegFile::get_name(rd), RegFile::get_name(rs1),
           RegFile::get_name(rs2));
}

void dump_b_type(const char *op, uint32_t rs1, uint32_t rs2, uint32_t pc) {
    printf("%s %s,%s,0x%x\n", op, RegFile::get_name(rs1),
           RegFile::get_name(rs2), pc);
}

void dump_mem_type(const char *op, uint32_t rd_rs2, uint32_t rs1,
                   uint32_t imm) {
    printf("%s %s,%d(%s)\n", op, RegFile::get_name(rd_rs2), imm,
           RegFile::get_name(rs1));
}

void load_segment(Mmu &mmu, std::ifstream &ifs, Elf32_Phdr ph) {
    if (ph.p_offset % page_size)
        fatal("%s: segment offset is not aligned to the page boundary",
              __func__);

    std::vector<uint8_t> buf(page_size);
    ifs.seekg(ph.p_offset, std::ios::beg);
    auto addr = ph.p_vaddr;
    // Load the segment page by page onto the memory
    for (long rem = ph.p_filesz; rem > 0; rem -= page_size) {
        auto readsize = (rem < page_size) ? rem : page_size;
        ifs.read(reinterpret_cast<char *>(buf.data()), readsize);
        buf.resize(readsize);
        mmu.write_page(addr, buf);
        addr += page_size;
    }

    printf("Loaded segment from 0x%x into 0x%x (size 0x%x)\n", ph.p_offset,
           ph.p_vaddr, ph.p_filesz);
}

bool validate_header(const Elf32_Ehdr &ehdr) {
    if (!(ehdr.e_ident[EI_MAG0] == ELFMAG0 &&
          ehdr.e_ident[EI_MAG1] == ELFMAG1 &&
          ehdr.e_ident[EI_MAG2] == ELFMAG2 && ehdr.e_ident[EI_MAG3] == ELFMAG3))
        return false;
    if (ehdr.e_ident[4] != ELFCLASS32)
        return false;
    if (ehdr.e_ident[5] != ELFDATA2LSB)
        return false;
    return true;
}
} // namespace

void Cpu::decode() {
  Instruction inst = instruction_buffer;
  MemAddr target_program_counter = ~0;
  uint8_t opcode = take_bits(inst, 0, 7);
  DecodeInfo di;

  // fprintf(stderr, "pc: 0x%x, inst: %08x\n", program_counter, inst);

  // Default nextPC = PC + 4
  // int len = decode_inst_length(mem, program_counter);
  int len = 4; // FIXME
  next_program_counter = program_counter + len;

  regs[zero] = 0;

  switch (opcode) {
  case OP_IMM:
    di = decode_i_type(inst);
    switch (di.funct3) {
    case F_ADDI:
      regs[di.rd] = regs[di.rs1] + sign_extend(di.imm, 12);
      dump_i_type("addi", di.rd, di.rs1, sign_extend(di.imm, 12));
      break;
    case F_SLTI: {
      int32_t rs1 = regs[di.rs1];
      int32_t imm = sign_extend(di.imm, 12);
      if (rs1 < imm) {
        regs[di.rd] = 1;
      } else {
        regs[di.rd] = 0;
      }
      dump_i_type("slti", di.rd, di.rs1, sign_extend(di.imm, 12));
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
      dump_i_type("sltiu", di.rd, di.rs1, sign_extend(di.imm, 12));
      break;
    }
    case F_ANDI:
      regs[di.rd] = regs[di.rs1] & sign_extend(di.imm, 12);
      dump_i_type("andi", di.rd, di.rs1, sign_extend(di.imm, 12));
      break;
    case F_ORI:
      regs[di.rd] = regs[di.rs1] | sign_extend(di.imm, 12);
      dump_i_type("ori", di.rd, di.rs1, sign_extend(di.imm, 12));
      break;
    case F_XORI:
      regs[di.rd] = regs[di.rs1] ^ sign_extend(di.imm, 12);
      dump_i_type("xori", di.rd, di.rs1, sign_extend(di.imm, 12));
      break;
    case F_SLLI:
      regs[di.rd] = regs[di.rs1] << di.imm;
      dump_i_type("slli", di.rd, di.rs1, sign_extend(di.imm, 12));
      break;
    case F_SRLI: {
      int shamt = di.imm & 0b11111;
      if ((di.imm >> 5) == 0) { // F_SRLI
        regs[di.rd] = regs[di.rs1] >> shamt;
        dump_i_type("srli", di.rd, di.rs1, shamt);
      } else { // F_SRAI
        regs[di.rd] = static_cast<int32_t>(regs[di.rs1]) >> shamt;
        dump_i_type("srai", di.rd, di.rs1, shamt);
      }
      break;
    }
    default:
      fatal("decode: unrecognized funct for OP_IMM");
      break;
    }
    break;
  case OP_LUI:
    di = decode_u_type(inst);
    regs[di.rd] = di.imm << 12;
    dump_u_type("lui", di.rd, di.imm);
    break;
  case OP_AUIPC:
    di = decode_u_type(inst);
    regs[di.rd] = program_counter + (di.imm << 12);
    dump_u_type("auipc", di.rd, di.imm);
    break;
  case OP_OP:
    di = decode_r_type(inst);
    switch (di.funct3) {
    case F_ADD: {           // F_SUB
      if (di.funct7 == 0) { // F_ADD
        regs[di.rd] = regs[di.rs1] + regs[di.rs2];
        dump_r_type("add", di.rd, di.rs1, di.rs2);
      } else { // F_SUB
        regs[di.rd] = regs[di.rs1] - regs[di.rs2];
        dump_r_type("sub", di.rd, di.rs1, di.rs2);
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
      dump_r_type("slt", di.rd, di.rs1, di.rs2);
      break;
    }
    case F_SLTU: {
      uint32_t rs1 = regs[di.rs1];
      uint32_t rs2 = regs[di.rs2];
      if (rs1 < rs2)
        regs[di.rd] = 1;
      else
        regs[di.rd] = 0;
      dump_r_type("sltu", di.rd, di.rs1, di.rs2);
      break;
    }
    case F_AND:
      regs[di.rd] = regs[di.rs1] & regs[di.rs2];
      dump_r_type("and", di.rd, di.rs1, di.rs2);
      break;
    case F_OR:
      regs[di.rd] = regs[di.rs1] | regs[di.rs2];
      dump_r_type("or", di.rd, di.rs1, di.rs2);
      break;
    case F_XOR:
      regs[di.rd] = regs[di.rs1] ^ regs[di.rs2];
      dump_r_type("xor", di.rd, di.rs1, di.rs2);
      break;
    case F_SLL: {
      int shamt = regs[di.rs2] & 0b11111;
      regs[di.rd] = regs[di.rs1] << shamt;
      dump_r_type("sll", di.rd, di.rs1, di.rs2);
      break;
    }
    case F_SRL: {
      int shamt = regs[di.rs2] & 0b11111;
      if (di.funct7 == 0) { // F_SRL
        regs[di.rd] = regs[di.rs1] >> shamt;
        dump_r_type("srl", di.rd, di.rs1, di.rs2);
      } else { // F_SRA
        regs[di.rd] = static_cast<int32_t>(regs[di.rs1]) >> shamt;
        dump_r_type("sra", di.rd, di.rs1, di.rs2);
      }
      break;
    }
    default:
      fatal("decode: unrecognized funct for OP");
      break;
    }
    break;
  case OP_JAL:
    di = decode_j_type(inst);
    next_program_counter = program_counter + sign_extend(di.imm, 20);
    regs[di.rd] = program_counter + len;
    printf("jal %s,0x%x\n", RegFile::get_name(di.rd), next_program_counter);
    break;
  case OP_JALR:
    di = decode_i_type(inst);
    // FIXME di.imm sign extend?
    next_program_counter = regs[di.rs1] + sign_extend(di.imm, 12);
    next_program_counter = next_program_counter >> 1 << 1;
    regs[di.rd] = program_counter + len;
    // TODO dump all the variants, e.g. jr and ret
    printf("jalr %s,%s,%+d\n", RegFile::get_name(di.rd),
           RegFile::get_name(di.rs1), sign_extend(di.imm, 12));
    break;
  case OP_BRANCH:
    di = decode_b_type(inst);
    target_program_counter = program_counter + sign_extend(di.imm, 12);
    switch (di.funct3) {
    case F_BEQ:
      if (regs[di.rs1] == regs[di.rs2])
        next_program_counter = target_program_counter;
      dump_b_type("beq", di.rs1, di.rs2, target_program_counter);
      break;
    case F_BNE:
      if (regs[di.rs1] != regs[di.rs2])
        next_program_counter = target_program_counter;
      dump_b_type("bne", di.rs1, di.rs2, target_program_counter);
      break;
    case F_BLT:
      if (static_cast<int32_t>(regs[di.rs1]) <
          static_cast<int32_t>(regs[di.rs2]))
        next_program_counter = target_program_counter;
      dump_b_type("blt", di.rs1, di.rs2, target_program_counter);
      break;
    case F_BLTU:
      if (regs[di.rs1] < regs[di.rs2])
        next_program_counter = target_program_counter;
      dump_b_type("bltu", di.rs1, di.rs2, target_program_counter);
      break;
    case F_BGE:
      if (static_cast<int32_t>(regs[di.rs1]) >=
          static_cast<int32_t>(regs[di.rs2]))
        next_program_counter = target_program_counter;
      dump_b_type("bge", di.rs1, di.rs2, target_program_counter);
      break;
    case F_BGEU:
      if (regs[di.rs1] >= regs[di.rs2])
        next_program_counter = target_program_counter;
      dump_b_type("bgeu", di.rs1, di.rs2, target_program_counter);
      break;
    default:
      fatal("decode: unrecognized funct for BRANCH");
      break;
    }
    break;
  case OP_LOAD: {
    di = decode_i_type(inst);
    MemAddr addr = regs[di.rs1] + sign_extend(di.imm, 12);

    switch (di.funct3) {
    case F_LB:
      regs[di.rd] = sign_extend(mmu.read8(addr), 8);
      dump_mem_type("lb", di.rd, di.rs1, sign_extend(di.imm, 12));
      break;
    case F_LBU:
      regs[di.rd] = static_cast<uint32_t>(mmu.read8(addr));
      dump_mem_type("lbu", di.rd, di.rs1, sign_extend(di.imm, 12));
      break;
    case F_LH:
      regs[di.rd] = sign_extend(mmu.read16(addr), 16);
      dump_mem_type("lh", di.rd, di.rs1, sign_extend(di.imm, 12));
      break;
    case F_LHU:
      regs[di.rd] = static_cast<uint32_t>(mmu.read16(addr));
      dump_mem_type("lhu", di.rd, di.rs1, sign_extend(di.imm, 12));
      break;
    case F_LW:
      regs[di.rd] = mmu.read32(addr);
      dump_mem_type("lw", di.rd, di.rs1, sign_extend(di.imm, 12));
      break;
    default:
      fatal("decode: unrecognized funct for LOAD");
      break;
    }
    break;
  }
  case OP_STORE: {
    di = decode_s_type(inst);
    MemAddr addr = regs[di.rs1] + sign_extend(di.imm, 12);

    switch (di.funct3) {
    case F_SB:
      dump_mem_type("sb", di.rs2, di.rs1, sign_extend(di.imm, 12));
      printf("storing %u (0x%x) to *0x%x\n", regs[di.rs2], regs[di.rs2], addr);
      mmu.write8(addr, regs[di.rs2]);
      break;
    case F_SH:
      dump_mem_type("sh", di.rs2, di.rs1, sign_extend(di.imm, 12));
      printf("storing %u (0x%x) to *0x%x\n", regs[di.rs2], regs[di.rs2], addr);
      mmu.write16(addr, regs[di.rs2]);
      break;
    case F_SW:
      dump_mem_type("sw", di.rs2, di.rs1, sign_extend(di.imm, 12));
      printf("storing %u (0x%x) to *0x%x\n", regs[di.rs2], regs[di.rs2], addr);
      mmu.write32(addr, regs[di.rs2]);
      break;
    default:
      fatal("decode: unrecognized funct for STORE");
      break;
    }
    break;
  }
  case OP_SYSTEM: {
    di = decode_i_type(inst);

    printf("ecall\n");

    switch (di.funct3) {
    case F_PRIV:
      if (regs[a0] == 93) {
        printf("return code was %d\n", regs[a0]);
        exit(0); // FIXME
      } else {
        mmu.page_table.print();
        fatal("decode: unimplemented ECALL: %d", regs[a7]);
      }
      break;
    }
    break;
  }
  default:
    fatal("decode: unrecognized opcode %x", opcode);
    break;
  }
}

void dump_regs(const RegFile &regs) {
    for (int i = 0; i < 32; i++) {
        printf("%3s: %#10x %9d ", RegFile::get_name(i), regs[i], regs[i]);
        if ((i + 1) % 4 == 0)
            printf("\n");
    }
    printf("\n");
}

void Cpu::cycle() {
    // Right now, decode decodes *and* also executes instructions.  This has to
    // be branched out as a separate function in the future.  Also, currently
    // this is a single-cycle implementation, not a pipelined one; i.e. fetch
    // and decode handle the same instruction.
    fetch();
    decode();
    printf("pc: 0x%x\n", program_counter);
    dump_regs(regs);
    n_cycle++;
}

void load_program(Cpu &cpu, const char *path) {
  std::ifstream ifs(path, std::ios::in | std::ios::binary);
  if (!ifs)
    fatal("failed to open file");

  // Validate the ELF file.
  Elf32_Ehdr elf_header;
  ifs.read(reinterpret_cast<char *>(&elf_header), sizeof(elf_header));
  if (!validate_header(elf_header))
    fatal("not a valid ELF32 file");

  printf("ELF: %d program headers\n", elf_header.e_phnum);
  printf("Program entry point: 0x%x\n", elf_header.e_entry);
  cpu.set_npc(elf_header.e_entry);

  // Read all the ELF program headers.
  std::vector<Elf32_Phdr> program_headers;
  for (int i = 0; i < elf_header.e_phnum; i++) {
    Elf32_Phdr ph;
    ifs.read(reinterpret_cast<char *>(&ph), sizeof(ph));
    program_headers.push_back(ph);
  }

  // For PT_LOAD headers, load the segments as specified.
  for (const auto ph : program_headers) {
    if (ph.p_type != PT_LOAD)
      continue;
    load_segment(cpu.get_mmu(), ifs, ph);
  }

  // Set the stack pointer.
  // cpu.regs[sp] = ~static_cast<uint32_t>(0);
  cpu.regs[sp] = 0xffffdd60; // FIXME: arbitrary value, taken from qemu-riscv32
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s EXEC-FILE\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  Memory mem;
  Cpu cpu(mem);

  load_program(cpu, argv[1]);

  while (true) {
    cpu.cycle();
  }

  printf("Simulated %ld cycles\n", cpu.n_cycle);
  return 0;
}
