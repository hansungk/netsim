#include "sim.h"
#include "router.h"
#include "queue.h"

int main(int argc, char **argv) {
    int debug = 0;
    bool verbose = false;
    double mean_interval = 0.0;
    long total_cycles = 10000;
    // Default is 4-ary 2-torus.
    int k = 4, r = 2;
    int terminal_count;
    int router_count;
    int radix;
    int vc_count = -1;

    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-d")) {
            debug = 1;
        } else if (!strcmp(argv[i], "-v")) {
            verbose = true;
        } else if (!strcmp(argv[i], "-k")) {
            i++;
            k = std::stoi(std::string(argv[i]));
        } else if (!strcmp(argv[i], "-r")) {
            i++;
            r = std::stoi(std::string(argv[i]));
        } else if (!strcmp(argv[i], "-vc")) {
            // VC can be overrided
            i++;
            vc_count = std::stoi(std::string(argv[i]));
        } else if (!strcmp(argv[i], "-cycle")) {
            i++;
            total_cycles = std::stoi(std::string(argv[i]));
        } else if (!strcmp(argv[i], "-interval")) {
            i++;
            mean_interval = std::stod(std::string(argv[i]));
        }
    }

    router_count = 1;
    for (int i = 0; i < r; i++) {
        router_count *= k;
    }
    terminal_count = router_count;
    // 1: terminal node, 2: bidirectional in each ring
    radix = 1 + 2 * r;
    if (vc_count == -1) {
        // 2 VCs in each dimension
        vc_count = 2 * r;
    } // else, overrided

    Topology top = topology_torus(k, r);

    Sim sim{verbose, debug, top, terminal_count, router_count, radix, vc_count, mean_interval, 10};
    // schedule(&sim.eventq, 0, tick_event_from_id(src_id(0)));
    // schedule(&sim.eventq, 0, tick_event_from_id(src_id(1)));
    // schedule(&sim.eventq, 0, tick_event_from_id(src_id(2)));
    // schedule(&sim.eventq, 0, tick_event_from_id(src_id(3)));

    // VC vs. Wormhole (6-ary 2-torus)
    // schedule(&sim.eventq, 0, tick_event_from_id(src_id(19)));
    // schedule(&sim.eventq, 0, tick_event_from_id(src_id(20)));

    for (int i = 0; i < terminal_count; i++) {
        schedule(&sim.eventq, 0, tick_event_from_id(src_id(i)));
    }

    sim_run(&sim, total_cycles);

    sim_report(&sim);

    sim_destroy(&sim);
    topology_destroy(&top);

    return 0;
}
