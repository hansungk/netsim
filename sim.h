#ifndef SIM_H
#define SIM_H

#include "event.h"
#include "router.h"
#include <memory>
#include <map>

void fatal(const char *fmt, ...);

// Encodes router topology in a bidirectional map.
// Supports runtime checking for connectivity error.
class Topology {
public:
    using RouterPortPair = std::pair<int /*router*/, int /*port*/>;

    RouterPortPair find(RouterPortPair input) {
        auto it = in_out_map.find(input);
        if (it == in_out_map.end()) {
            return not_found;
        } else {
            return it->second;
        }
    }

    bool connect(const RouterPortPair input, const RouterPortPair output);

    static constexpr RouterPortPair not_found{-1, -1};

private:
    std::map<RouterPortPair, RouterPortPair> in_out_map;
    std::map<RouterPortPair, RouterPortPair> out_in_map;
};

class Sim {
public:
    Sim(int router_count, int radix, Topology &top);

    // Run the simulator.
    void run();
    // Process an event.
    void process(const Event &e);

    void handler();

    EventQueue eventq; // global event queue
    Topology &topology;
    std::vector<Router> routers;
};

#endif
