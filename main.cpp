#include "sim.h"
#include "router.h"
#include "queue.h"

int main(int argc, char **argv) {
    int debug = 0;
    bool verbose = false;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-d")) {
            debug = 1;
        } else if (!strcmp(argv[i], "-v")) {
            verbose = true;
        }
    }

    Topology top = topology_torus(4, 2);

    Sim sim{verbose, debug, top, 16, 16, 5, 4, 10};
    // schedule(&sim.eventq, 0, tick_event_from_id(src_id(0)));
    // schedule(&sim.eventq, 1, tick_event_from_id(src_id(1)));
    // schedule(&sim.eventq, 2, tick_event_from_id(src_id(2)));
    // schedule(&sim.eventq, 3, tick_event_from_id(src_id(3)));
    for (int i = 0; i < 16; i++) {
        schedule(&sim.eventq, 0, tick_event_from_id(src_id(i)));
    }

    sim_run(&sim, 10000);

    sim_report(&sim);

    sim_destroy(&sim);
    topology_destroy(&top);

    return 0;
}
