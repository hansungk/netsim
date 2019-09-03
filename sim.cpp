#include "sim.h"
#include <iostream>

bool Topology::connect(const RouterPortPair input,
                       const RouterPortPair output) {
    auto insert_success = in_out_map.insert({input, output}).second;
    if (!out_in_map.insert({output, input}).second) {
        // Bad connectivity: destination port is already connected
        return false;
    }
    return insert_success;
}

Sim::Sim(int router_count, int radix, Topology &top) : eventq(), topology(top) {
    for (int id = 0; id < router_count; id++) {
        std::vector<Topology::RouterPortPair> dest_ports;
        for (int port = 0; port < radix; port++) {
            dest_ports.push_back(topology.find({id, port}));
        }
        routers.emplace_back(eventq, id, radix, dest_ports);
    }
}

void Sim::run() {
    while (!eventq.empty()) {
        auto e = eventq.pop();
        process(e);
    }
}

void Sim::process(const Event &e) {
    e.f(routers[e.id]);
}
