#ifndef SIM_H
#define SIM_H

#include "event.h"

void fatal(const char *fmt, ...);

class Sim {
public:
    Sim() : eventq{} {}

    // Run the simulator.
    void run();

    void handler();

    EventQueue eventq; // main event queue
};

#endif
