#include "sim.h"
#include "router.h"
#include "queue.h"

int main(int argc, char **argv) {
    int debug = 0;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-d"))
            debug = 1;
    }

    Topology top = topology_torus(4, 2);

    Sim sim{debug, top, 16, 16, 5, 7};
    for (int i = 0; i < 16; i++) {
        schedule(&sim.eventq, i, tick_event_from_id(src_id(i)));
    }

    sim_run(&sim, 100000);

    sim_report(&sim);

    sim_destroy(&sim);
    topology_destroy(&top);

    return 0;
    }
