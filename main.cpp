#include "sim.h"
#include "router.h"
#include <iostream>

int main(void) {
    Sim sim{4};
    sim.eventq.schedule(0, Event{0, [](Router &r) { r.put(0, Flit{0}); }});
    // sim.eventq.schedule(0, Event{[&] { sim.routers[0].input_units[0].put(Flit{1}); }});
    // sim.eventq.schedule(0, Event{[&] { sim.routers[0].input_units[0].put(Flit{2}); }});

    sim.run();

    return 0;
}
