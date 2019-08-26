#include "router.h"
#include <iostream>

Router::Router(EventQueue &eq, int radix)
    : eventq(eq) {
    for (int i = 0; i < radix; i++) {
        input_units.push_back(
            InputUnit{*this, Event{[this, i] { route_compute(i); }}});
        output_units.push_back(OutputUnit{});
    }
}

void Router::InputUnit::put(const Flit &flit) {
    // If the buffer was empty, kickstart the pipeline
    if (buf.empty()) {
        router.eventq.reschedule(1, drain_event);
    }

    buf.push_back(flit);
}

void Router::route_compute(int port) {
    std::cout << "[" << input_units[port].buf.front().flit_num << "] route computation\n";

    eventq.reschedule(1, Event{[this, port] { vc_alloc(port); }});
}

void Router::vc_alloc(int port) {
    std::cout << "[" << input_units[port].buf.front().flit_num << "] vc allocation\n";

    eventq.reschedule(1, Event{[this, port] { switch_alloc(port); }});
}

void Router::switch_alloc(int port) {
    std::cout << "[" << input_units[port].buf.front().flit_num << "] switch allocation\n";

    eventq.reschedule(1, Event{[this, port] { switch_traverse(port); }});
}

void Router::switch_traverse(int port) {
    std::cout << "[" << input_units[port].buf.front().flit_num << "] switch traverse\n";

    Flit flit = input_units[port].buf.front();
    input_units[port].buf.pop_front();
    output_units[port].buf.push_back(flit);

    if (!input_units[port].buf.empty()) {
        eventq.reschedule(1, Event{[this, port] { route_compute(port); }});
    }
}

void Router::run() {
    while (!eventq.empty()) {
        auto e = eventq.pop();
        std::cout << "[event @ t=" << eventq.time() << ":]\n";
        e.func();
    }
}
