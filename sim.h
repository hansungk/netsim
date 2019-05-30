// -*- C++ -*-
#ifndef SIM_H
#define SIM_H

#include "cpu.h"
#include "memory.h"
#include "event.h"

class Sim {
public:
    Sim(const Cpu &cpu_, const Memory &mem_) : cpu(cpu_), mem(mem_) {}

    // Run the simulator.
    void run();

    Cpu cpu;
    Memory mem;
    EventQueue event_queue; // main event queue
};

#endif
