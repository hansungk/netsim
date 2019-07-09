#include "sim.h"
#include "event.h"
#include <cstdarg>
#include <cstdio>
#include <elf.h>
#include <iostream>
#include <vector>

void fatal(const char *fmt, ...) {
    va_list ap;

    fprintf(stderr, "fatal: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");

    exit(EXIT_FAILURE);
}

namespace {
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

// Load an ELF program at `path` into memory and initialize architectural
// states for execution.
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
    cpu.regs[sp] =
        0xffffdd60; // FIXME: arbitrary value, taken from qemu-riscv32
}
} // namespace

void Sim::handler() { std::cout << "memory finished!\n"; }

void Sim::run() {
    cpu.inst_reg.watch([this] { cpu.decode_and_execute(); });
    eventq.schedule(0, {[this] { cpu.fetch(); }});

    while (!eventq.empty()) {
        auto e = eventq.pop();
        std::cout << "[event @ t=" << eventq.time() << ":]\n";
        e.func();
    }

    // std::cout << "val = " << val << std::endl;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s EXEC-FILE\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    Sim sim;
    // Memory mem{sim};
    // Cpu cpu{sim, mem};

    load_program(sim.cpu, argv[1]);

    sim.run();

    // while (true) {
    //     cpu.cycle();
    // }

    return 0;
}
