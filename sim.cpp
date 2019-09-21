#include "sim.h"
#include <iostream>

Sim::Sim(int terminal_count, int router_count, int radix, Topology &top)
    : topology(top) {
    // Initialize terminal nodes
    for (int id = 0; id < terminal_count; id++) {
        std::vector<Topology::RouterPortPair> src_port_dests;
        std::vector<Topology::RouterPortPair> dst_port_dests; // empty
        for (int port = 0; port < radix; port++) {
            src_port_dests.push_back(topology.find({SrcId{id}, port}));
        }
        src_nodes.emplace_back(eventq, NodeType::Source, id, radix, src_port_dests);
        // Destination nodes doesn't have output ports!
        dst_nodes.emplace_back(eventq, NodeType::Destination, id, radix, dst_port_dests);
    }

    // Initializes routers
    for (int id = 0; id < router_count; id++) {
        std::vector<Topology::RouterPortPair> dest_ports;
        for (int port = 0; port < radix; port++) {
            dest_ports.push_back(topology.find({RtrId{id}, port}));
        }
        routers.emplace_back(eventq, NodeType::Router, id, radix, dest_ports);
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
