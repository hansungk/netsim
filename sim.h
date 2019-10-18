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
    void run(long until = -1);
    // Process an event.
    void process(const Event &e);
    void report() const;

    void handler();

    EventQueue eventq{}; // global event queue
    Stat stat;
    Topology &topology;
    std::map<std::pair<RouterPortPair, RouterPortPair>, Channel &> channel_map{};
    std::vector<Channel> channels{};
    std::vector<Router> routers{};
    std::vector<Router> src_nodes{};
    std::vector<Router> dst_nodes{};
};

#endif
