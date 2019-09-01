#include "sim.h"
#include <iostream>

Sim::Sim(int router_count) : eventq{} {
    for (int id = 0; id < router_count; id++) {
        routers.emplace_back(eventq, id, 4);
    }
}

void Sim::run() {
    while (!eventq.empty()) {
        auto e = eventq.pop();
        std::cout << "[event @ t=" << eventq.time() << ":]\n";
        process(e);
    }
}

void Sim::process(const Event &e) {
    e.f(routers[e.id]);
}
