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
  if (program_counter < mem.size) {
    program_counter = next_program_counter;
    // TODO: Big endian.
    instruction_buffer = mem.fetch32(program_counter);
  }
}

static void dump_i_type(const char *op, uint32_t rd, uint32_t rs1,
                        uint32_t imm) {
  printf("%s %s,%s,%d\n", op, RegFile::get_name(rd), RegFile::get_name(rs1),
         imm);
}

static void dump_u_type(const char *op, uint32_t rd, uint32_t imm) {
  printf("%s %s,0x%x\n", op, RegFile::get_name(rd), imm);
}

static void dump_r_type(const char *op, uint32_t rd, uint32_t rs1,
                        uint32_t rs2) {
  printf("%s %s,%s,%s\n", op, RegFile::get_name(rd), RegFile::get_name(rs1),
         RegFile::get_name(rs2));
}

static void dump_b_type(const char *op, uint32_t rs1, uint32_t rs2,
                        uint32_t pc) {
  printf("%s %s,%s,0x%x\n", op, RegFile::get_name(rs1), RegFile::get_name(rs2),
         pc);
}

static void dump_mem_type(const char *op, uint32_t rd_rs2, uint32_t rs1,
                          uint32_t imm) {
  printf("%s %s,%d(%s)\n", op, RegFile::get_name(rd_rs2), imm,
         RegFile::get_name(rs1));
}

void Cpu::decode() {
  Instruction inst = instruction_buffer;
  MemAddr target_program_counter = ~0;
  uint8_t opcode = take_bits(inst, 0, 7);
  DecodeInfo di;

  // fprintf(stderr, "pc: 0x%x, inst: %08x\n", program_counter, inst);

  // Default nextPC = PC + 4
  int len = decode_inst_length(mem, program_counter);
  next_program_counter = program_counter + len;

  regs[0] = 0;

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
      regs[di.rd] = sign_extend(mem.fetch8(addr), 8);
      dump_mem_type("lb", di.rd, di.rs1, sign_extend(di.imm, 12));
      break;
    case F_LBU:
      regs[di.rd] = static_cast<uint32_t>(mem.fetch8(addr));
      dump_mem_type("lbu", di.rd, di.rs1, sign_extend(di.imm, 12));
      break;
    case F_LH:
      regs[di.rd] = sign_extend(mem.fetch16(addr), 16);
      dump_mem_type("lh", di.rd, di.rs1, sign_extend(di.imm, 12));
      break;
    case F_LHU:
      regs[di.rd] = static_cast<uint32_t>(mem.fetch16(addr));
      dump_mem_type("lhu", di.rd, di.rs1, sign_extend(di.imm, 12));
      break;
    case F_LW:
      regs[di.rd] = mem.fetch32(addr);
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
      mem.store8(addr, regs[di.rs2]);
      break;
    case F_SH:
      dump_mem_type("sh", di.rs2, di.rs1, sign_extend(di.imm, 12));
      printf("storing %u (0x%x) to *0x%x\n", regs[di.rs2], regs[di.rs2], addr);
      mem.store16(addr, regs[di.rs2]);
      break;
    case F_SW:
      dump_mem_type("sw", di.rs2, di.rs1, sign_extend(di.imm, 12));
      printf("storing %u (0x%x) to *0x%x\n", regs[di.rs2], regs[di.rs2], addr);
      mem.store32(addr, regs[di.rs2]);
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
      if (regs[17] == 93) {
        printf("return code was %d\n", regs[10]);
        exit(0); // FIXME
      } else
        fatal("decode: unimplemented ECALL: %d", regs[17]);
      break;
    }
    break;
  }
  default:
    fatal("decode: unrecognized opcode %x", opcode);
    break;
  }
}

void Cpu::dump_regs() {
  printf("pc: 0x%x\n", program_counter);
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
  dump_regs();
  n_cycle++;
}

static void load_segment(Memory &mem, std::ifstream &ifs, Elf32_Phdr ph) {
  ifs.seekg(ph.p_offset, std::ios::beg);
  char *inbuf = reinterpret_cast<char *>(mem.data.get() + ph.p_vaddr);
  ifs.read(inbuf, ph.p_filesz);
  printf("Loaded segment from 0x%x into 0x%x (size 0x%x)\n", ph.p_offset,
         ph.p_vaddr, ph.p_filesz);
}

static bool check_valid_header(const Elf32_Ehdr &ehdr) {
  if (!(ehdr.e_ident[0] == EI_MAG0 &&
        ehdr.e_ident[1] == EI_MAG1 &&
        ehdr.e_ident[2] == EI_MAG2 &&
        ehdr.e_ident[3] == EI_MAG3))
    return false;
  if (ehdr.e_ident[4] != ELFCLASS32)
    return false;
  if (ehdr.e_ident[5] != ELFDATA2LSB)
    return false;
  return true;
}

void Cpu::load_program(const char *path) {
  std::ifstream ifs(path, std::ios::in | std::ios::binary);
  if (!ifs)
    fatal("failed to open file");

  // Validate the ELF file.
  Elf32_Ehdr elf_header;
  ifs.read(reinterpret_cast<char *>(&elf_header), sizeof(elf_header));
  if (!check_valid_header(elf_header))
    fatal("not a valid ELF32 file");

  next_program_counter = elf_header.e_entry;
  printf("ELF: %d program headers\n", elf_header.e_phnum);

  std::vector<Elf32_Phdr> program_headers;

  // Read all the ELF program headers.
  for (int i = 0; i < elf_header.e_phnum; i++) {
    Elf32_Phdr ph;
    ifs.read(reinterpret_cast<char *>(&ph), sizeof(ph));
    program_headers.push_back(ph);
  }

  // For PT_LOAD headers, load the segments as specified.
  for (const auto ph : program_headers) {
    if (ph.p_type != PT_LOAD)
      continue;
    load_segment(mem, ifs, ph);
  }

  // Set stack pointer.
  regs[2] = 0x7fffff;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s EXEC-FILE\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  Memory mem(16 * 1024 * 1024);
  Cpu cpu(mem);

  cpu.load_program(argv[1]);

  printf("Program entry point: 0x%x\n", cpu.program_counter);
  printf("Entry instruction: ");
  for (int i = 0; i < 4; i++)
    printf("%02x ", mem.data[cpu.program_counter + i]);
  printf("\n");

  while (true) {
    cpu.cycle();
  }

  printf("Simulated %ld cycles\n", cpu.n_cycle);
  return 0;
}
