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
        routers.emplace_back(eventq, id, radix);
    }
}

void Sim::run() {
    while (!eventq.empty()) {
        auto e = eventq.pop();
        std::cout << "[event @ t=" << eventq.curr_time() << ":]\n";
        process(e);
    }
}

void Sim::process(const Event &e) {
    e.f(routers[e.id]);
}
