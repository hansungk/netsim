#include "router.h"
#include <iostream>

int main(void) {
    EventQueue eq;
    Router router{eq, 4};
    eq.schedule(0, Event{[&] { router.input_units[0].put(Flit{0}); }});
    eq.schedule(0, Event{[&] { router.input_units[1].put(Flit{1}); }});
    eq.schedule(0, Event{[&] { router.input_units[2].put(Flit{2}); }});

    router.run();

    // std::cout << "out_buf len: " << router.out_buf.size() << std::endl;

    return 0;
}
