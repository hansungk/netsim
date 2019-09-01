#include "router.h"
#include <iostream>
#include <cassert>

Router::Router(EventQueue &eq, int id_, int radix)
    : eventq(eq), tick_event(id_, [](Router &r) { r.tick(); }), id(id_) {
    std::cout << "Router::Router(id=" << id << ")\n";

    for (int port = 0; port < radix; port++) {
        input_units.push_back(InputUnit{});
        output_units.push_back(OutputUnit{});
    }
}

void Router::put(int port, const Flit &flit) {
    std::cout << "Put!\n";

    // If the buffer was empty, kickstart the pipeline
    if (input_units[port].buf.empty()) {
        eventq.reschedule(1, tick_event);
    }

    input_units[port].buf.push_back(flit);
}

void Router::tick() {
    std::cout << "Tick!\n";

    // Self-tick automatically unless all input ports are empty.
    // FIXME: accuracy?
    bool empty = true;
    for (int i = 0; i < get_radix(); i++) {
        if (!input_units[i].buf.empty()) {
            empty = false;
        }
    }
    if (!empty) {
        eventq.reschedule(1, tick_event);
    }
}

void Router::route_compute(int port) {
    std::cout << "route_compute(), router id=" << id << ", port=" << port << "\n";
    assert(!input_units[port].buf.empty());
    std::cout << "[" << input_units[port].buf.front().flit_num << "] route computation\n";

    eventq.reschedule(1, tick_event);
}

void Router::vc_alloc(int port) {
    std::cout << "[" << input_units[port].buf.front().flit_num << "] vc allocation\n";

    eventq.reschedule(1, tick_event);
}

void Router::switch_alloc(int port) {
    std::cout << "[" << input_units[port].buf.front().flit_num << "] switch allocation\n";

    eventq.reschedule(1, tick_event);
}

void Router::switch_traverse(int port) {
    std::cout << "[" << input_units[port].buf.front().flit_num << "] switch traverse\n";

    Flit flit = input_units[port].buf.front();
    input_units[port].buf.pop_front();
    output_units[port].buf.push_back(flit);

    if (!input_units[port].buf.empty()) {
        eventq.reschedule(1, tick_event);
    }
}
