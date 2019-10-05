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
    auto insert_success = forward_map.insert({input, output}).second;
    if (!reverse_map.insert({output, input}).second) {
        // Bad connectivity: destination port is already connected
        return false;
    }
    return insert_success;
}

Router::Router(EventQueue &eq, NodeId id_, int radix,
               const std::vector<Topology::RouterPortPair> &in_origs,
               const std::vector<Topology::RouterPortPair> &out_dsts)
    : id(id_), eventq(eq), tick_event(id_, [](Router &r) { r.tick(); }),
      input_origins(in_origs), output_destinations(out_dsts) {
    for (int port = 0; port < radix; port++) {
        input_units.emplace_back();
        output_units.emplace_back();
    }
}

std::ostream &Router::dbg() const {
    auto &out = std::cout;
    out << "[@" << std::setw(3) << eventq.curr_time() << "] ";
    out << "[" << id << "] ";
    return out;
}

void Router::put(int port, const Flit flit) {
    assert(port < get_radix() && "no such port!");
    dbg() << "[" << flit.payload << "] Put!\n";
    auto &iu = input_units[port];

    // If the buffer was empty, set stage to RC, and kickstart the pipeline
    if (iu.buf.empty()) {
        // Idle -> RC transition
        iu.state.global = InputUnit::State::GlobalState::Routing;
        iu.stage = PipelineStage::RC;
        // TODO: check if already scheduled
        if (eventq.curr_time() != last_reschedule_tick) {
            eventq.reschedule(1, tick_event);
            dbg() << "scheduled tick to " << eventq.curr_time() + 1
                  << std::endl;
            last_reschedule_tick = eventq.curr_time();
        }
    }

    // FIXME: Hardcoded buffer size limit
    assert(iu.buf.size() < 8 && "Input buffer overflow!");

    iu.buf.push_back(flit);
}

void Router::put_credit(int port, const Credit credit) {
    assert(port < get_radix() && "no such port!");
    dbg() << "Put_credit!\n";

    if (eventq.curr_time() != last_reschedule_tick) {
        eventq.reschedule(1, tick_event);
        dbg() << "scheduled tick to " << eventq.curr_time() + 1 << std::endl;
        last_reschedule_tick = eventq.curr_time();
    }

    auto &ou = output_units[port];
    ou.buf_credit = credit;
}

void Router::source_generate() {
    auto &ou = output_units[0];

    if (ou.state.credit_count > 0) {
        // TODO: All flits go to node #2!
        Flit flit{Flit::Type::Head, std::get<SrcId>(id).id, 2,
                  flit_payload_counter};
        flit_payload_counter++;

        assert(get_radix() == 1);
        auto dst_pair = output_destinations[0];
        assert(dst_pair != Topology::not_connected);

        eventq.reschedule(1, Event{dst_pair.first, [=](Router &r) {
                                       r.put(dst_pair.second, flit);
                                   }});
        ou.state.credit_count--;
        assert(ou.state.credit_count >= 0);

        dbg() << "[" << flit.payload << "] Flit created!\n";
    } else {
        dbg() << "Credit stall!\n";
    }
}

void Router::tick() {
    // Make sure this router has not been already ticked in this cycle.
    if (eventq.curr_time() == last_tick) {
        dbg() << "WARN: double tick! curr_time=" << eventq.curr_time()
              << ", last_tick=" << last_tick << std::endl;
        return;
    }
    // assert(eventq.curr_time() != last_tick);

    reschedule_next_tick = false;

    // Different tick actions for different types of node.
    if (is_source(id)) {
        source_generate();
        // Source nodes also needs to manage credit in order to send flits at
        // the right time.
        credit_update();

        // TODO: for now, infinitely generate flits.
        mark_self_reschedule();
    } else if (is_destination(id)) {
        auto &iu = input_units[0];

        if (!iu.buf.empty()) {
            dbg() << "[" << iu.buf.front().payload << "] Flit arrived!\n";
            iu.buf.pop_front();

            auto src_pair = input_origins[0];
            assert(src_pair != Topology::not_connected);
            eventq.reschedule(1, Event{src_pair.first, [=](Router &r) {
                                           r.put_credit(src_pair.second,
                                                        Credit{});
                                       }});

            // Self-tick autonomously unless all input ports are empty.
            mark_self_reschedule();
        }
    } else {
        // Process each pipeline stage.
        // Stages are processed in reverse dependency order to prevent
        // coherence bug.  E.g., if a flit succeeds in route_compute() and
        // advances to the VA stage, and then vc_alloc() is called, it would
        // then get processed again in the same cycle.
        switch_traverse();
        switch_alloc();
        vc_alloc();
        route_compute();
        credit_update();

        // Self-tick autonomously unless all input ports are empty.
        // FIXME: accuracy?
        bool empty = true;
        for (int i = 0; i < get_radix(); i++) {
            if (!input_units[i].buf.empty()) {
                empty = false;
                break;
            }
        }
        if (!empty) {
            mark_self_reschedule();
        }
    }

    // Do the rescheduling at here once to prevent flooding the event queue.
    if (reschedule_next_tick && eventq.curr_time() != last_reschedule_tick) {
        eventq.reschedule(1, tick_event);
        // dbg() << "self-rescheduled to " << eventq.curr_time() + 1 << std::endl;
        // XXX: Hacky!
        last_reschedule_tick = eventq.curr_time();
    }

    last_tick = eventq.curr_time();
}

///
/// Pipeline stages
///

void Router::credit_update() {
    for (int port = 0; port < get_radix(); port++) {
        auto &ou = output_units[port];
        if (ou.buf_credit) {
            dbg() << "Credit update! credit=" << ou.state.credit_count << "->"
                  << ou.state.credit_count + 1 << "\n";
            ou.state.credit_count++;
            ou.buf_credit = std::nullopt;
        }
    }
}

void Router::route_compute() {
    for (int port = 0; port < get_radix(); port++) {
        auto &iu = input_units[port];

        if (iu.stage == PipelineStage::RC) {
            auto flit = iu.buf.front();
            dbg() << "[" << flit.payload
                  << "] route computation (dst:" << flit.route_info.dst
                  << ")\n";
            assert(!iu.buf.empty());

            // TODO: Simple algorithmic routing: keep rotating clockwise until
            // destination is met.
            if (flit.route_info.dst == std::get<RtrId>(id).id) {
                // Port 0 is always connected to a terminal node
                iu.state.route_port = 0;
            } else {
                iu.state.route_port = 2;
            }
            auto &ou = output_units[iu.state.route_port];
            ou.state.input_port = port;

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
        auto &ou = output_units[iu.state.route_port];

        if (iu.stage == PipelineStage::VA) {
            if (iu.state.global == InputUnit::State::GlobalState::VCWait) {
                dbg() << "[" << iu.buf.front().payload << "] VC allocation\n";
                assert(!iu.buf.empty());

                // TODO: VA always succeeds as of now.
                iu.state.global = InputUnit::State::GlobalState::Active;
                ou.state.global = OutputUnit::State::GlobalState::Active;
            }

            // VA -> SA transition
            // Only the VCs with available credits can proceed to the SA stage.
            if (ou.state.credit_count > 0) {
                iu.stage = PipelineStage::SA;
                mark_self_reschedule();
            } else {
                dbg() << "Credit stall! credit=" << ou.state.credit_count << "\n";

                iu.state.global = InputUnit::State::GlobalState::CreditWait;
                ou.state.global = OutputUnit::State::GlobalState::CreditWait;
            }
        }
    }
}

void Router::switch_alloc() {
    for (int port = 0; port < get_radix(); port++) {
        auto &iu = input_units[port];
        auto &ou = output_units[iu.state.route_port];

        if (iu.stage == PipelineStage::SA) {
            dbg() << "[" << iu.buf.front().payload << "] switch allocation\n";
            assert(!iu.buf.empty());

            // TODO: SA always succeeds as of now.

            // SA -> ST transition
            iu.state.global = InputUnit::State::GlobalState::Active;
            iu.stage = PipelineStage::ST;

            // CT stage: return credit to the upstream node.
            auto src_pair = input_origins[port];
            assert(src_pair != Topology::not_connected);
            // FIXME: link traversal time fixed to 1
            dbg() << "Credit sent to {" << src_pair.first << ", "
                  << src_pair.second << "}\n";
            eventq.reschedule(1, Event{src_pair.first, [=](Router &r) {
                                           r.put_credit(src_pair.second,
                                                        Credit{});
                                       }});
            dbg() << "Credit decrement, credit=" << ou.state.credit_count
                  << "->" << ou.state.credit_count - 1 << ";\n";
            ou.state.credit_count--;
            assert(ou.state.credit_count >= 0);

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
            auto dst_pair = output_destinations[iu.state.route_port];
            assert(dst_pair != Topology::not_connected);
            // FIXME: link traversal time fixed to 1
            dbg() << "Flit sent to {" << dst_pair.first << ", "
                  << dst_pair.second << "}\n";
            eventq.reschedule(1, Event{dst_pair.first, [=](Router &r) {
                                           r.put(dst_pair.second, flit);
                                       }});

            // With output speedup:
            // auto &ou = output_units[iu.state.route_port];
            // ou.buf.push_back(flit);

            // ST -> ?? transition
            // TODO: if tail flit, switch global state to idle.
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
