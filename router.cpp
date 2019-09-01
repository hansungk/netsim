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
    auto &iu = input_units[port];

    // If the buffer was empty, set stage to RC, and kickstart the pipeline
    if (iu.buf.empty()) {
        // Idle -> RC transition
        iu.state.global = InputUnit::State::GlobalState::Routing;
        iu.stage = PipelineStage::RC;
        eventq.reschedule(1, tick_event);
    }

    iu.buf.push_back(flit);
}

void Router::tick() {
    // Make sure this router has not been already ticked in this cycle.
    assert(eventq.curr_time() != last_tick);
    std::cout << "Tick!\n";

    // Process each pipeline stage.
    route_compute();
    vc_alloc();

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

    last_tick = eventq.curr_time();
}

///
/// Pipeline stages
///

void Router::route_compute() {
    std::cout << "route_compute(), router id=" << id << std::endl;
    for (int port = 0; port < get_radix(); port++) {
        auto &input_unit = input_units[port];
        if (input_unit.stage == PipelineStage::RC) {
            std::cout << "[" << port << "] route computation\n";
            assert(!input_unit.buf.empty());

            // TODO: simple routing: input port == output port
            input_unit.state.route = port;

            // RC -> VA transition
            input_unit.state.global = InputUnit::State::GlobalState::VCWait;
            eventq.reschedule(1, tick_event);
        }
    }
}

void Router::vc_alloc() {
    // std::cout << "[" << input_units[port].buf.front().flit_num << "] vc allocation\n";
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
