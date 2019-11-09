#include "sim.h"
#include "router.h"
#include "queue.h"

int main(int argc, char **argv) {
    int debug = 0;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-d"))
            debug = 1;
    }

    Topology top = topology_ring(4);

    Sim sim{debug, 4, 4, 3, top};
    sim.eventq.schedule(0, tick_event_from_id(src_id(0)));
    sim.eventq.schedule(0, tick_event_from_id(src_id(1)));
    sim.eventq.schedule(0, tick_event_from_id(src_id(2)));
    // sim.eventq.schedule(0, tick_event_from_id(src_id(3)));

    sim.run(10000);

    sim.report();

    sim_destroy(&sim);
    topology_destroy(&top);

    return 0;
}
