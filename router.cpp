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

Topology Topology::ring(int n) {
    Topology top;

    // Port usage: 0:terminal, 1:left, 2:right
    // Inter-router channels
    for (int i = 0; i < n; i++) {
        int l = i;
        int r = (i + 1) % n;
        RouterPortPair lport{RtrId{l}, 2};
        RouterPortPair rport{RtrId{r}, 1};
        // Bidirectional channel
        top.connect(lport, rport);
        top.connect(rport, lport);
    }

    // Terminal node channels
    for (int i = 0; i < n; i++) {
        RouterPortPair src_port{SrcId{i}, 0};
        RouterPortPair dst_port{DstId{i}, 0};
        RouterPortPair rtr_port{RtrId{i}, 0};
        top.connect(src_port, rtr_port);
        top.connect(rtr_port, dst_port);
    }

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

std::ostream &operator<<(std::ostream &out, const Flit &flit) {
    out << "{" << flit.route_info.src << "." << flit.payload << "}";
    return out;
}

Router::Router(EventQueue &eq, NodeId id_, int radix,
               const std::vector<Topology::RouterPortPair> &in_origs,
               const std::vector<Topology::RouterPortPair> &out_dsts)
    : id(id_), eventq(eq), tick_event(id_, [](Router &r) { r.tick(); }),
      input_origins(in_origs), output_destinations(out_dsts) {
    for (int port = 0; port < radix; port++) {
        input_units.emplace_back();
        output_units.emplace_back(input_buf_size);
    }

    if (is_source(id) || is_destination(id)) {
        assert(input_units.size() == 1);
        assert(output_units.size() == 1);
        input_units[0].state.route_port = 0;
        output_units[0].state.input_port = 0;
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
    auto &iu = input_units[port];

    // If the buffer was empty, this is the only place to kickstart the
    // pipeline.
    if (iu.buf.empty()) {
        // If the input unit state was also idle (empty != idle!), set
        // the stage to RC.
        if (iu.stage == PipelineStage::Idle) {
            assert(iu.state.global == InputUnit::State::GlobalState::Idle);

            // Idle -> RC transition
            iu.state.global = InputUnit::State::GlobalState::Routing;
            iu.stage = PipelineStage::RC;
        }

        if (eventq.curr_time() != last_reschedule_tick) {
            eventq.reschedule(1, tick_event);
            // dbg() << "scheduled tick to " << eventq.curr_time() + 1
            //       << std::endl;
            last_reschedule_tick = eventq.curr_time();
        }
    }

    // FIXME: Hardcoded buffer size limit
    assert(iu.buf.size() < input_buf_size && "Input buffer overflow!");
    iu.buf.push_back(flit);

    dbg() << flit << " Put! buf.size()=" << iu.buf.size() << "\n";
}

void Router::put_credit(int port, const Credit credit) {
    assert(port < get_radix() && "no such port!");
    dbg() << "Put_credit!\n";

    if (eventq.curr_time() != last_reschedule_tick) {
        eventq.reschedule(1, tick_event);
        // dbg() << "scheduled tick to " << eventq.curr_time() + 1 << std::endl;
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

        dbg() << flit << " Flit created and sent!\n";

        // TODO: for now, infinitely generate flits.
        mark_self_reschedule();
    } else {
        dbg() << "Credit stall!\n";
    }
}

void Router::destination_consume() {
    auto &iu = input_units[0];

    if (!iu.buf.empty()) {
        dbg() << "Destination buf size=" << iu.buf.size() << std::endl;
        dbg() << iu.buf.front() << " Flit arrived!\n";
        iu.buf.pop_front();
        // assert(iu.buf.empty());

        auto src_pair = input_origins[0];
        assert(src_pair != Topology::not_connected);
        eventq.reschedule(1, Event{src_pair.first, [=](Router &r) {
                                       r.put_credit(src_pair.second, Credit{});
                                   }});

        // Self-tick autonomously unless all input ports are empty.
        mark_self_reschedule();
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
    } else if (is_destination(id)) {
        destination_consume();
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
        // FIXME: redundant?
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

        // Reschedule every cycle (~cycle-accurate simulation):
        // mark_self_reschedule();
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

            // Upon credit update, the input and output unit receiving this
            // credit may or may not be in the CreditWait state.  If they are,
            // make sure to switch them back to the active state so that they
            // can proceed in the SA stage.
            //
            // This can otherwise be implemented in the SA stage itself,
            // switching the stage to Active and simultaneously commencing to
            // the switch allocation.  However, the CreditWait stage doesn't
            // seem to serve much purpose in that case.  This implementation is
            // what I think of as a more natural one.
            assert(ou.state.input_port != -1); // XXX: redundant?
            auto &iu = input_units[ou.state.input_port];
            if (iu.state.global == InputUnit::State::GlobalState::CreditWait) {
                assert(ou.state.global == OutputUnit::State::GlobalState::CreditWait);
                iu.state.global = InputUnit::State::GlobalState::Active;
                ou.state.global = OutputUnit::State::GlobalState::Active;
                mark_self_reschedule();
                dbg() << "credit update with kickstart!\n";
            } else if (ou.state.credit_count == 1) {
                // XXX: This is for waking up the source node, but is it
                // necessary for other types of node?
                mark_self_reschedule();
                dbg() << "credit update with kickstart!\n";
            } else {
                dbg() << "credit update, but no kickstart\n";
            }
        }
    }
}

void Router::route_compute() {
    for (int port = 0; port < get_radix(); port++) {
        auto &iu = input_units[port];

        if (iu.stage == PipelineStage::RC) {
            auto flit = iu.buf.front();
            dbg() << flit << " route computation\n";
            assert(!iu.buf.empty());

            // TODO: Simple algorithmic routing: keep rotating clockwise until
            // destination is met.
            if (flit.route_info.dst == std::get<RtrId>(id).id) {
                // Port 0 is always connected to a terminal node
                iu.state.route_port = 0;
            } else {
                iu.state.route_port = 2;
            }

            dbg() << flit << " RC success (port=" << iu.state.route_port << ")\n";

            // RC -> VA transition
            iu.state.global = InputUnit::State::GlobalState::VCWait;
            iu.stage = PipelineStage::VA;
            mark_self_reschedule();
        }
    }
}

// This function expects the given output VC to be in the Idle state.
int Router::vc_arbit_round_robin(int out_port) {
    int iport = last_grant_input;

    for (int i = 0; i < get_radix(); i++) {
        auto &iu = input_units[iport];

        if (iu.stage == PipelineStage::VA && iu.state.route_port == out_port) {
            // XXX: is VA stage and VCWait state the same?
            assert(iu.state.global == InputUnit::State::GlobalState::VCWait);
            last_grant_input = iport;
            return iport;
        }

        iport = (iport + 1) % get_radix();
    }

    // Indicates that there was no request for this VC.
    return -1;
}

void Router::vc_alloc() {
    dbg() << "VC allocation\n";

    for (int oport = 0; oport < get_radix(); oport++) {
        auto &ou = output_units[oport];

        // Only do arbitration for inactive output VCs.
        if (ou.state.global == OutputUnit::State::GlobalState::Idle) {
            // Arbitration
            int iport = vc_arbit_round_robin(oport);

            if (iport == -1) {
                // dbg() << "no pending VC request!\n";
            } else {
                auto &iu = input_units[iport];

                // Gates open!
                iu.state.global = InputUnit::State::GlobalState::Active;
                ou.state.global = OutputUnit::State::GlobalState::Active;

                // Record the input port to the Output unit.
                ou.state.input_port = iport;

                iu.stage = PipelineStage::SA;
                mark_self_reschedule();

                dbg() << "VA success for" << iu.buf.front() << std::endl;
            }
        }
    }
}

void Router::switch_alloc() {
    for (int port = 0; port < get_radix(); port++) {
        auto &iu = input_units[port];
        auto &ou = output_units[iu.state.route_port];

        if (iu.stage == PipelineStage::SA) {
            if (iu.state.global == InputUnit::State::GlobalState::Active) {
                // Input units in the active state *may* be empty, e.g. if
                // their body flits have not arrived.  Check that.
                if (iu.buf.empty()) {
                    continue;
                }

                dbg() << iu.buf.front() << " switch allocation\n";

                // Check credit first; if no credit available, switch to
                // CreditWait state and do not attempt allocation at all.
                if (ou.state.credit_count <= 0) {
                    dbg() << "[port " << iu.state.route_port
                          << "] Credit stall! credit=" << ou.state.credit_count
                          << "\n";
                    iu.state.global = InputUnit::State::GlobalState::CreditWait;
                    ou.state.global =
                        OutputUnit::State::GlobalState::CreditWait;
                    // From now on, SA will not be attempted on this flit
                    // unless the CU stage bumps the state to Active.
                } else {
                    assert(ou.state.global ==
                           OutputUnit::State::GlobalState::Active);
                    assert(!iu.buf.empty());

                    // TODO: SA always succeeds as of now.

                    // SA -> ST transition
                    iu.state.global = InputUnit::State::GlobalState::Active;
                    iu.stage = PipelineStage::ST;
                    mark_self_reschedule();

                    // CT stage: return credit to the upstream node.
                    // FIXME: shouldn't this have timing difference with SA?
                    auto src_pair = input_origins[port];
                    assert(src_pair != Topology::not_connected);
                    // FIXME: link traversal time fixed to 1
                    dbg() << "Credit sent to {" << src_pair.first << ", "
                          << src_pair.second << "}\n";
                    eventq.reschedule(1, Event{src_pair.first, [=](Router &r) {
                                                   r.put_credit(src_pair.second,
                                                                Credit{});
                                               }});
                    dbg() << "[port " << iu.state.route_port
                          << "] Credit decrement, credit="
                          << ou.state.credit_count << "->"
                          << ou.state.credit_count - 1 << ";\n";
                    ou.state.credit_count--;
                    assert(ou.state.credit_count >= 0);
                }
            }
        }
    }
}

void Router::switch_traverse() {
    for (int port = 0; port < get_radix(); port++) {
        auto &iu = input_units[port];

        if (iu.stage == PipelineStage::ST) {
            dbg() << iu.buf.front() << " switch traverse\n";
            assert(!iu.buf.empty());

            Flit flit = iu.buf.front();
            iu.buf.pop_front();

            // No output speedup: there is no need for an output buffer
            // (Ch17.3).  Flits that exit the switch are directly placed on the
            // channel.
            auto dst_pair = output_destinations[iu.state.route_port];
            assert(dst_pair != Topology::not_connected);
            // FIXME: link traversal time fixed to 1
            dbg() << "Flit " << flit << " sent to {" << dst_pair.first << ", "
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

            // FIXME: check if tail flit and if so, switch back to Idle
            iu.stage = PipelineStage::SA;
            mark_self_reschedule();
        }
    }
}
