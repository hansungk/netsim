#include "sim.h"
#include <iostream>

Sim::Sim(int terminal_count, int router_count, int radix, Topology &top)
    : topology(top) {
    // Initialize terminal nodes
    for (int id = 0; id < terminal_count; id++) {
        std::vector<Topology::RouterPortPair> src_port_origs; // empty
        std::vector<Topology::RouterPortPair> src_port_dests;
        std::vector<Topology::RouterPortPair> dst_port_origs;
        std::vector<Topology::RouterPortPair> dst_port_dests; // empty

        // Terminal nodes only have a single port.  Also, destination nodes
        // doesn't have output ports!
        src_port_dests.push_back(topology.find_forward({SrcId{id}, 0}));
        dst_port_origs.push_back(topology.find_reverse({DstId{id}, 0}));

        src_nodes.emplace_back(eventq, SrcId{id}, 1, src_port_origs, src_port_dests);
        dst_nodes.emplace_back(eventq, DstId{id}, 1, dst_port_origs, dst_port_dests);
    }

    // Initialize router nodes
    for (int id = 0; id < router_count; id++) {
        std::vector<Topology::RouterPortPair> port_origs;
        std::vector<Topology::RouterPortPair> port_dests;

        for (int port = 0; port < radix; port++) {
            port_origs.push_back(topology.find_reverse({RtrId{id}, port}));
            port_dests.push_back(topology.find_forward({RtrId{id}, port}));
        }

        routers.emplace_back(eventq, RtrId{id}, radix, port_origs, port_dests);
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
