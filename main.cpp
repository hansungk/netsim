#include "sim.h"
#include "router.h"
#include "queue.h"
#include <iostream>

int main(void) {
    Topology top = topology_ring(4);

    Sim sim{4, 4, 3, top};
    sim.eventq.schedule(0, tick_event_from_id(src_id(0)));
    sim.eventq.schedule(0, tick_event_from_id(src_id(1)));
    sim.eventq.schedule(0, tick_event_from_id(src_id(2)));

    sim.run(10000);

    sim_destroy(&sim);
    topology_destroy(&top);

    return 0;
}
