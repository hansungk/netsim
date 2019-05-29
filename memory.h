// -*- C++ -*-
#ifndef MEMORY_H
#define MEMORY_H

#include <array>
#include <cstdint>
#include <fstream>
#include <vector>
#include <map>
#include <unordered_set>
#include <optional>

using MemAddr = uint32_t;
using Page = std::array<uint8_t, 4096>;
using Vpn = uint32_t;
using Ppn = uint32_t;

constexpr auto page_size = 4096;
constexpr auto page_bits = 12; // FIXME: log?
constexpr auto page_offset_mask = ~((~0u) << page_bits);

constexpr Vpn get_vpn(MemAddr v_addr) { return v_addr >> page_bits; }

/// A page table entry.
struct Pte {
  uint16_t asid = 0; // address-space identifier.  Currently this stays at 0
                     // because only one thread can be executed.
  uint32_t vpn;      // virtual page number
  uint32_t ppn;      // physical page number
  // @future: protection bits
};

/// The page table.
///
/// TODO: Currently, the page table is implemented as a simple std::map.
/// Implement a proper hierarchicical or inverted page table in the future.
class PageTable {
public:
    std::optional<Pte> lookup(Vpn vpn);
    // Add a new PTE to the page table.
    void add(Vpn vpn, Ppn ppn);
    void print() const;

private:
    std::map<Vpn, Pte> map;
};

/// Memory implements the physical memory.
class Memory {
public:
    Memory() = default;

    uint64_t size() const { return size_; }
    uint8_t *data() { return buf.data(); }

    // Read/write operations on the physical memory.  All addressses are
    // physical.
    uint32_t read32(MemAddr p_addr);
    uint16_t read16(MemAddr p_addr);
    uint8_t read8(MemAddr p_addr);
    void write32(MemAddr p_addr, uint32_t value);
    void write16(MemAddr p_addr, uint16_t value);
    void write8(MemAddr p_addr, uint8_t value);
    // Write a whole page onto the memory.  This is mainly a convenience
    // function for the loading of a program.  @future: may be replaced by
    // mmap().
    void write_page(MemAddr p_addr, const std::vector<uint8_t> &page);

    // Allocate a new physical frame.  This allocates more memory to the
    // simulator.
    // Returns the page frame number of the newly allocated frame.
    Ppn new_frame();

private:
    uint64_t size_{};
    std::vector<uint8_t> buf{};
    std::unordered_set<uint32_t /* page frame number */> used_page_frames{};
};

/// Memory management unit (MMU).
class Mmu {
public:
    Mmu(Memory &mem_) : mem(mem_) {}

    // All read/write operations pass through MMU.  All addresses are virtual.
    uint32_t read32(MemAddr addr);
    uint16_t read16(MemAddr addr);
    uint8_t read8(MemAddr addr);
    void write32(MemAddr addr, uint32_t value);
    void write16(MemAddr addr, uint16_t value);
    void write8(MemAddr addr, uint8_t value);
    // Write a whole page onto the memory.  This is mainly a convenience
    // function for the loading of a program.  @future: may be replaced by
    // mmap().
    void write_page(Vpn vpn, const std::vector<uint8_t> &page);

    // Virtual to physical address translation.
    MemAddr translate(MemAddr v_addr);

    PageTable page_table;

private:
    Memory &mem; // physical DRAM managed by this MMU.
};

#endif
