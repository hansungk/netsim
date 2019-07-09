#ifndef SIM_H
#define SIM_H

#include "cpu.h"
#include "memory.h"
#include "event.h"

void fatal(const char *fmt, ...);

class Sim {
public:
    Sim() : eventq{}, mem{eventq}, cpu{eventq, mem} {}

    // Run the simulator.
    void run();

    void handler();

    EventQueue eventq; // main event queue
    Memory mem;
    Cpu cpu;
};

#endif
