#include "sim.h"
#include "router.h"
#include <iostream>

int main(void) {
    Topology top{
        // Router nodes
        {{0, 2}, {1, 2}},
        {{1, 3}, {0, 3}},
        {{1, 2}, {2, 2}},
        {{2, 3}, {1, 3}},
        {{2, 2}, {3, 2}},
        {{3, 3}, {2, 3}},
        {{3, 2}, {0, 2}},
        {{0, 3}, {3, 3}},
        // Terminal nodes
        {{Topology::src(0), 0}, {0, 0}},
        {{0, 0}, {Topology::dst(0), 0}},
        {{Topology::src(1), 0}, {1, 0}},
        {{1, 0}, {Topology::dst(1), 0}},
        {{Topology::src(2), 0}, {2, 0}},
        {{2, 0}, {Topology::dst(2), 0}},
        {{Topology::src(3), 0}, {3, 0}},
        {{3, 0}, {Topology::dst(3), 0}},
    };

    Sim sim{4, 4, top};
    sim.eventq.schedule(0, Event{0, [](Router &r) { r.put(2, Flit{Flit::Type::Head, 0}); }});
    sim.eventq.schedule(1, Event{0, [](Router &r) { r.put(2, Flit{Flit::Type::Body, 1}); }});
    sim.eventq.schedule(2, Event{0, [](Router &r) { r.put(2, Flit{Flit::Type::Body, 2}); }});
    sim.eventq.schedule(3, Event{0, [](Router &r) { r.put(2, Flit{Flit::Type::Body, 3}); }});

    sim.run();

    return 0;
}
