#ifndef SIM_H
#define SIM_H

#include "event.h"
#include "router.h"
#include <memory>
#include <map>

void fatal(const char *fmt, ...);

class Sim {
public:
    Sim(int terminal_count, int router_count, int radix, Topology &top);

    // Run the simulator.
    void run();
    // Process an event.
    void process(const Event &e);

    void handler();

    EventQueue eventq{}; // global event queue
    Topology &topology;
    std::vector<Router> routers{};
    std::vector<Router> src_nodes{};
    std::vector<Router> dst_nodes{};
};

#endif
