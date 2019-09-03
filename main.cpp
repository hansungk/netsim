#include "sim.h"
#include "router.h"
#include <iostream>

int main(void) {
    Topology top;
    auto conn =
        top.connect({0, 0}, {1, 0}) &&
        top.connect({1, 0}, {2, 0}) &&
        top.connect({2, 0}, {3, 0});
    if (!conn) {
        std::cerr << "error: bad connectivity\n";
        exit(EXIT_FAILURE);
    }

    Sim sim{4, 1, top};
    sim.eventq.schedule(0, Event{0, [](Router &r) { r.put(0, Flit{Flit::Type::head, 0}); }});
    sim.eventq.schedule(1, Event{0, [](Router &r) { r.put(0, Flit{Flit::Type::body, 1}); }});
    sim.eventq.schedule(2, Event{0, [](Router &r) { r.put(0, Flit{Flit::Type::body, 2}); }});
    sim.eventq.schedule(3, Event{0, [](Router &r) { r.put(0, Flit{Flit::Type::body, 3}); }});

    sim.run();

    return 0;
}
