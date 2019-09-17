#include "sim.h"
#include <iostream>

Sim::Sim(int terminal_count, int router_count, int radix, Topology &top)
    : topology(top), src_nodes(terminal_count), dst_nodes(terminal_count) {
    for (int id = 0; id < router_count; id++) {
        // Initialize port destination designators for each router
        std::vector<Topology::RouterPortPair> dest_ports;
        for (int port = 0; port < radix; port++) {
            dest_ports.push_back(topology.find({RtrId{id}, port}));
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
    if (std::holds_alternative<SrcId>(e.id)) {
        e.f(src_nodes[std::get<SrcId>(e.id).id]);
    } else if (std::holds_alternative<DstId>(e.id)) {
        e.f(dst_nodes[std::get<DstId>(e.id).id]);
    } else {
        e.f(routers[std::get<RtrId>(e.id).id]);
    }
}
