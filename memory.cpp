#include "memory.h"
#include "event.h"
#include <iostream>

// Address translation
//
// The address translation logic does not keep any information on which page
// frames are allocated.  As a result, it _always_ allocates new page frame on a
// page fault.  This works fine for programs with correct memory behaviors.  For
// programs with memory bugs, it may end up allocating exceedingly large amount
// of memory, but it is currently not a priority to provide diagnostics for
// faulty programs.  TODO.

std::optional<Pte> PageTable::lookup(uint32_t vpn) {
    auto f = map.find(vpn);
    if (f == map.end())
        return {};
    return f->second;
}

void PageTable::add(Vpn vpn, Ppn ppn) {
    auto pte = Pte{.vpn = vpn, .ppn = ppn};
    map.insert({vpn, pte});
}

void PageTable::print() const {
    for (const auto &pair : map) {
        std::cout << "[" << pair.first << " -> " << pair.second.ppn << "]\n";
    }
}

Ppn Memory::new_frame() {
    buf.resize(buf.size() + page_size);
    // It is important to call end() after resize() as resize() can invalidate
    // end()s.
    return (buf.size() - page_size) / page_size;
}

// TODO: readN() assumes little endianness of the memory system. Maybe improve
// these in the future to be endian-agnostic.
// TODO: profile to see if inline is worthy.

uint32_t Memory::read32(Req<uint32_t> &req, MemAddr p_addr) {
    uint32_t val = *reinterpret_cast<const uint32_t *>(&buf[p_addr]);
    eventq.schedule(Event{6, [&req, val]() { req.reply(val); }});
    return val;
}

uint16_t Memory::read16(MemAddr p_addr) {
    return *reinterpret_cast<const uint16_t *>(&buf[p_addr]);
}

uint8_t Memory::read8(MemAddr p_addr) {
    return *reinterpret_cast<const uint8_t *>(&buf[p_addr]);
}

void Memory::write32(MemAddr p_addr, uint32_t value) {
    *reinterpret_cast<uint32_t *>(&buf[p_addr]) = value;
}

void Memory::write16(MemAddr p_addr, uint16_t value) {
    *reinterpret_cast<uint16_t *>(&buf[p_addr]) = value;
}

void Memory::write8(MemAddr p_addr, uint8_t value) {
    *reinterpret_cast<uint8_t *>(&buf[p_addr]) = value;
}

void Memory::write_page(MemAddr p_addr, const std::vector<uint8_t> &page) {
    std::copy(page.begin(), page.end(), buf.begin() + p_addr);
}

// -----------------------------------------------------------------------------
// Memory Management Unit (MMU)
// -----------------------------------------------------------------------------

// Translate address using the given PTE.
static MemAddr translate(Pte pte, MemAddr v_addr) {
    return (pte.ppn << page_bits) | (v_addr & page_offset_mask);
}

// @future: page fault is currently handled altogether in this function, i.e. by
// the hardware.  Need to consider if address translation and page fault
// handling logic should be split into two separate things.
MemAddr Mmu::translate(MemAddr v_addr) {
    auto pte = page_table.lookup(get_vpn(v_addr));
    if (pte) {
        return ::translate(pte.value(), v_addr);
    } else {
        // Page fault.
        std::cout << "Page fault"
                  << "\n";
        // page_table.print();
        Ppn ppn = mem.new_frame();
        page_table.add(get_vpn(v_addr), ppn);
        // This time, the translation should not fail.
        return translate(v_addr);
    }
}

uint32_t Mmu::read32(MemAddr addr) {
    auto p_addr = translate(addr);
    // return mem.read32(&v, p_addr);
    return 0;
}
uint16_t Mmu::read16(MemAddr addr) {
    auto p_addr = translate(addr);
    return mem.read16(p_addr);
}
uint8_t Mmu::read8(MemAddr addr) {
    auto p_addr = translate(addr);
    return mem.read8(p_addr);
}
void Mmu::write32(MemAddr addr, uint32_t value) {
    auto p_addr = translate(addr);
    mem.write32(p_addr, value);
}
void Mmu::write16(MemAddr addr, uint16_t value) {
    auto p_addr = translate(addr);
    mem.write16(p_addr, value);
}
void Mmu::write8(MemAddr addr, uint8_t value) {
    auto p_addr = translate(addr);
    mem.write8(p_addr, value);
}
void Mmu::write_page(MemAddr addr, const std::vector<uint8_t> &page) {
    auto p_addr = translate(addr);
    mem.write_page(p_addr, page);
}
