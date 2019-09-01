#ifndef SIM_H
#define SIM_H

#include "event.h"
#include "router.h"
#include <memory>

void fatal(const char *fmt, ...);

class Sim {
public:
    Sim(int router_count);

    // Run the simulator.
    void run();
    // Process an event.
    void process(const Event &e);

    void handler();

    EventQueue eventq; // global event queue
    std::vector<Router> routers;
};

#endif
