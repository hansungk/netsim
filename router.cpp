#include "router.h"
#include <cassert>
#include <iomanip>
#include <iostream>

Topology::Topology(
    std::initializer_list<std::pair<RouterPortPair, RouterPortPair>> pairs) {
    for (auto [src, dst] : pairs) {
        if (!connect(src, dst)) {
            // TODO: fail gracefully
            std::cerr << "fatal: connectivity error" << std::endl;
            exit(EXIT_FAILURE);
        }
    }
}

Topology Topology::ring() {
    Topology top;
    // TODO
    return top;
}

bool Topology::connect(const RouterPortPair input,
                       const RouterPortPair output) {
    auto insert_success = in_out_map.insert({input, output}).second;
    if (!out_in_map.insert({output, input}).second) {
        // Bad connectivity: destination port is already connected
        return false;
    }
    return insert_success;
}

Router::Router(EventQueue &eq, NodeId id_, int radix,
               const std::vector<Topology::RouterPortPair> &dp)
    : eventq(eq), tick_event(id_, [](Router &r) { r.tick(); }),
      output_destinations(dp), id(id_) {
    for (int port = 0; port < radix; port++) {
        input_units.emplace_back();
        output_units.emplace_back();
    }
}

std::ostream &Router::dbg() const {
    auto &out = std::cout;
    out << "[@" << std::setw(3) << eventq.curr_time() << "] ";
    return out;
}

void Router::put(int port, const Flit &flit) {
    assert(port < get_radix() && "no such port!");
    dbg() << "[" << flit.payload << "] Put!\n";
    auto &iu = input_units[port];

    // If the buffer was empty, set stage to RC, and kickstart the pipeline
    if (iu.buf.empty()) {
        // Idle -> RC transition
        iu.state.global = InputUnit::State::GlobalState::Routing;
        iu.stage = PipelineStage::RC;
        eventq.reschedule(1, tick_event);
        // eventq.print_and_exit();
    }

    // FIXME: Hardcoded buffer size limit
    assert(iu.buf.size() < 8 && "Input buffer overflow!");

    iu.buf.push_back(flit);
}

void Router::tick() {
    // Make sure this router has not been already ticked in this cycle.
    // std::cout << "Router " << id << ", curr_time=" << eventq.curr_time()
    //           << ", last_tick=" << last_tick << std::endl;
    assert(eventq.curr_time() != last_tick);
    reschedule_next_tick = false;

    // Different tick actions for different types of node.
    if (is_source(id)) {
        // TODO: check credit
        Flit flit{Flit::Type::Head, std::get<SrcId>(id).id, 2, 0};

        assert(get_radix() == 1);
        auto dst_pair = output_destinations[0];
        if (dst_pair != Topology::not_connected) {
            eventq.reschedule(1, Event{dst_pair.first, [=](Router &r) {
                                           r.put(dst_pair.second, flit);
                                       }});
        }
        std::cout << "Source node!\n";

        // TODO: for now, infinitely generate flits.
        mark_self_reschedule();
    } else if (is_destination(id)) {
        std::cout << "Destination node!\n";
    } else {
        // Process each pipeline stage.
        // Stages are processed in reverse order to prevent coherence bug.  E.g.,
        // if a flit succeeds in route_compute() and advances to the VA stage, and
        // then vc_alloc() is called, it would then get processed again in the same
        // cycle.
        switch_traverse();
        switch_alloc();
        vc_alloc();
        route_compute();

        // Self-tick autonomously unless all input ports are empty.
        // FIXME: accuracy?
        bool empty = true;
        for (int i = 0; i < get_radix(); i++) {
            if (!input_units[i].buf.empty()) {
                std::cout << "IU size=" << input_units[i].buf.size() << std::endl;
                empty = false;
                break;
            }
        }
        if (!empty) {
            mark_self_reschedule();
        }
    }

    // Do the rescheduling at here once to prevent flooding the event queue.
    if (reschedule_next_tick) {
        eventq.reschedule(1, tick_event);
    }

    last_tick = eventq.curr_time();
}

///
/// Pipeline stages
///

void Router::route_compute() {
    for (int port = 0; port < get_radix(); port++) {
        auto &iu = input_units[port];
        if (iu.stage == PipelineStage::RC) {
            auto flit = iu.buf.front();
            dbg() << "[" << flit.payload
                  << "] route computation (dst:" << flit.route_info.dst
                  << ")\n";
            assert(!iu.buf.empty());

            // TODO: simple routing: input port == output port
            iu.state.route = port;

            // RC -> VA transition
            iu.state.global = InputUnit::State::GlobalState::VCWait;
            iu.stage = PipelineStage::VA;
            mark_self_reschedule();
        }
    }
}

void Router::vc_alloc() {
    for (int port = 0; port < get_radix(); port++) {
        auto &iu = input_units[port];
        if (iu.stage == PipelineStage::VA) {
            dbg() << "[" << iu.buf.front().payload << "] VC allocation\n";
            assert(!iu.buf.empty());

            // VA -> SA transition
            iu.state.global = InputUnit::State::GlobalState::Active;
            iu.stage = PipelineStage::SA;
            mark_self_reschedule();
        }
    }
}

void Router::switch_alloc() {
    for (int port = 0; port < get_radix(); port++) {
        auto &iu = input_units[port];
        if (iu.stage == PipelineStage::SA) {
            dbg() << "[" << iu.buf.front().payload << "] switch allocation\n";
            assert(!iu.buf.empty());

            // SA -> ST transition
            iu.state.global = InputUnit::State::GlobalState::Active;
            iu.stage = PipelineStage::ST;
            mark_self_reschedule();
        }
    }
}

void Router::switch_traverse() {
    for (int port = 0; port < get_radix(); port++) {
        auto &iu = input_units[port];
        if (iu.stage == PipelineStage::ST) {
            dbg() << "[" << iu.buf.front().payload << "] switch traverse\n";
            assert(!iu.buf.empty());

            Flit flit = iu.buf.front();
            iu.buf.pop_front();

            // No output speedup: there is no need for an output buffer
            // (Ch17.3).  Flits that exit the switch are directly placed on the
            // channel.
            auto dst_pair = output_destinations[port];
            if (dst_pair != Topology::not_connected) {
                // FIXME: link traversal time fixed to 1
                eventq.reschedule(1, Event{dst_pair.first, [=](Router &r) {
                                               r.put(dst_pair.second, flit);
                                           }});
            }

            // With output speedup:
            // auto &ou = output_units[iu.state.route];
            // ou.buf.push_back(flit);

            // ST -> ?? transition
            iu.state.global = InputUnit::State::GlobalState::Active;

            if (iu.buf.empty()) {
                iu.stage = PipelineStage::Idle;
            } else {
                // FIXME: what if the next flit is a head flit?
                iu.stage = PipelineStage::SA;
                mark_self_reschedule();
            }
        }
    }
}
