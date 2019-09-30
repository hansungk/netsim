#include "sim.h"
#include "router.h"
#include <iostream>

int main(void) {
    Topology top{
        {{RtrId{0}, 2}, {RtrId{1}, 2}},
        {{RtrId{1}, 3}, {RtrId{0}, 3}},
        {{RtrId{1}, 2}, {RtrId{2}, 2}},
        {{RtrId{2}, 3}, {RtrId{1}, 3}},
        {{RtrId{2}, 2}, {RtrId{3}, 2}},
        {{RtrId{3}, 3}, {RtrId{2}, 3}},
        {{RtrId{3}, 2}, {RtrId{0}, 2}},
        {{RtrId{0}, 3}, {RtrId{3}, 3}},
        // Terminal nodes
        // {{SrcId{0}, 0}, {RtrId{0}, 0}},
        // {{RtrId{0}, 0}, {DstId{0}, 0}},
        // {{SrcId{1}, 0}, {RtrId{1}, 0}},
        // {{RtrId{1}, 0}, {DstId{1}, 0}},
        // {{SrcId{2}, 0}, {RtrId{2}, 0}},
        // {{RtrId{2}, 0}, {DstId{2}, 0}},
        // {{SrcId{3}, 0}, {RtrId{3}, 0}},
        // {{RtrId{3}, 0}, {DstId{3}, 0}},
    };

    Sim sim{4, 4, 4, top};
    sim.eventq.schedule(0, Event{SrcId{0}, [](Router &r) {
                                     r.put(2, Flit{Flit::Type::Head, 0});
                                 }});
    // sim.eventq.schedule(1, Event{RtrId{0}, [](Router &r) {
    //                                  r.put(2, Flit{Flit::Type::Body, 1});
    //                              }});
    // sim.eventq.schedule(2, Event{RtrId{0}, [](Router &r) {
    //                                  r.put(2, Flit{Flit::Type::Body, 2});
    //                              }});
    // sim.eventq.schedule(3, Event{RtrId{0}, [](Router &r) {
    //                                  r.put(2, Flit{Flit::Type::Body, 3});
    //                              }});

    sim.run();

    return 0;
}
