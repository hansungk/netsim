#include "sim.h"
#include "router.h"
#include <iostream>

int main(void) {
    Topology top = topology_ring(4);

    Sim sim{4, 4, 3, top};
    sim.eventq.schedule(0, sim.src_nodes[0].get_tick_event());
    sim.eventq.schedule(0, sim.src_nodes[1].get_tick_event());
    sim.eventq.schedule(0, sim.src_nodes[2].get_tick_event());
    // sim.eventq.schedule(0, sim.src_nodes[3].get_tick_event());
    // sim.eventq.schedule(1, Event{RtrId{0}, [](Router &r) {
    //                                  r.put(2, Flit{Flit::Type::Body, 1});
    //                              }});
    // sim.eventq.schedule(2, Event{RtrId{0}, [](Router &r) {
    //                                  r.put(2, Flit{Flit::Type::Body, 2});
    //                              }});
    // sim.eventq.schedule(3, Event{RtrId{0}, [](Router &r) {
    //                                  r.put(2, Flit{Flit::Type::Body, 3});
    //                              }});

    sim.run(10000);

    topology_destroy(&top);

    return 0;
}
