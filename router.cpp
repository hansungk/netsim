#include "router.h"
#include <cassert>
#include <iomanip>
#include <iostream>

namespace {
Event tick_event_from_id(Id id) {
    return Event{id, [](Router &r) { r.tick(); }};
}
} // namespace

Channel::Channel(EventQueue &eq, Id id_, const long dl, const RouterPortPair s,
                 const RouterPortPair d)
    : src(s), dst(d), eventq(eq), delay(dl),
      tick_event(tick_event_from_id(id_)) {}

void Channel::put(const Flit &flit) {
    buf.push_back({eventq.curr_time() + delay, flit});
    eventq.reschedule(delay, tick_event_from_id(dst.first));
}

void Channel::put_credit(const Credit &credit) {
    buf_credit.push_back({eventq.curr_time() + delay, credit});
    eventq.reschedule(delay, tick_event_from_id(src.first));
}

std::optional<Flit> Channel::get() {
    auto front = buf.cbegin();
    if (!buf.empty() && eventq.curr_time() >= front->first) {
        assert(eventq.curr_time() == front->first && "stagnant flit!");
        Flit flit = front->second;
        buf.pop_front();
        return flit;
    } else {
        return {};
    }
}

std::optional<Credit> Channel::get_credit() {
    auto front = buf_credit.cbegin();
    if (!buf_credit.empty() && eventq.curr_time() >= front->first) {
        assert(eventq.curr_time() == front->first && "stagnant flit!");
        buf_credit.pop_front();
        return front->second;
    } else {
        return {};
    }
}

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
    std::vector<int> ids;
    bool res = true;

    for (int id = 0; id < n; id++) {
        ids.push_back(id);
    }

    // Inter-router channels
    res &= top.connect_ring(ids);
    // Terminal node channels
    res &= top.connect_terminals(ids);

    assert(res);
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

bool Topology::connect_terminals(const std::vector<int> &ids) {
    bool res = true;

    for (auto id : ids) {
        RouterPortPair src_port{SrcId{id}, 0};
        RouterPortPair dst_port{DstId{id}, 0};
        RouterPortPair rtr_port{RtrId{id}, 0};

        // Bidirectional channel
        res &= connect(src_port, rtr_port);
        res &= connect(rtr_port, dst_port);
        if (!res) {
            return false;
        }
    }

    return true;
}

// Port usage: 0:terminal, 1:counter-clockwise, 2:clockwise
bool Topology::connect_ring(const std::vector<int> &ids) {
    bool res = true;

    for (size_t i = 0; i < ids.size(); i++) {
        int l = ids[i];
        int r = ids[(i + 1) % ids.size()];
        RouterPortPair lport{RtrId{l}, 2};
        RouterPortPair rport{RtrId{r}, 1};

        // Bidirectional channel
        res &= connect(lport, rport);
        res &= connect(rport, lport);
        if (!res) {
            return false;
        }
    }

    return true;
}

std::ostream &operator<<(std::ostream &out, const Flit &flit) {
    out << "{" << flit.route_info.src << "." << flit.payload << "}";
    return out;
}

Router::Router(EventQueue &eq, Id id_, int radix,
               const ChannelRefVec &in_chs,
               const ChannelRefVec &out_chs)
    : id(id_), eventq(eq), tick_event(tick_event_from_id(id_)),
      input_channels(in_chs), output_channels(out_chs) {
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

void Router::do_reschedule() {
    if (reschedule_next_tick && eventq.curr_time() != last_reschedule_tick) {
        eventq.reschedule(1, tick_event);
        // dbg() << "self-rescheduled to " << eventq.curr_time() + 1 <<
        // std::endl;
        // XXX: Hacky!
        last_reschedule_tick = eventq.curr_time();
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
        fetch_credit();
    } else if (is_destination(id)) {
        destination_consume();
        fetch_flit();
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
        fetch_credit();
        fetch_flit();

        // Self-tick autonomously unless all input ports are empty.
        // FIXME: redundant?
        // bool empty = true;
        // for (int i = 0; i < get_radix(); i++) {
        //     if (!input_units[i].buf.empty()) {
        //         empty = false;
        //         break;
        //     }
        // }
        // if (!empty) {
        //     mark_reschedule();
        // }
    }

    // Do the rescheduling at here once to prevent flooding the event queue.
    do_reschedule();

    last_tick = eventq.curr_time();
}

///
/// Pipeline stages
///

void Router::source_generate() {
    auto &ou = output_units[0];

    if (ou.state.credit_count <= 0) {
        dbg() << "Credit stall!\n";
        return;
    }

    // TODO: All flits go to node #2!
    Flit flit{Flit::Type::Body, std::get<SrcId>(id).id, 2,
              flit_payload_counter};
    if (flit_payload_counter == 0) {
        flit.type = Flit::Type::Head;
        flit_payload_counter++;
    } else if (flit_payload_counter == 4 /* FIXME */) {
        flit.type = Flit::Type::Tail;
        flit_payload_counter = 0;
    } else {
        flit_payload_counter++;
    }

    assert(get_radix() == 1);
    auto out_ch = output_channels[0];
    out_ch.get().put(flit);

    dbg() << "Credit decrement, credit=" << ou.state.credit_count << "->"
          << ou.state.credit_count - 1 << ";\n";
    ou.state.credit_count--;
    assert(ou.state.credit_count >= 0);

    flit_generate_count++;
    dbg() << flit << " Flit created and sent!\n";

    // TODO: for now, infinitely generate flits.
    mark_reschedule();
}

void Router::destination_consume() {
    auto &iu = input_units[0];

    if (!iu.buf.empty()) {
        dbg() << "Destination buf size=" << iu.buf.size() << std::endl;
        dbg() << iu.buf.front() << " Flit arrived!\n";

        flit_arrive_count++;
        iu.buf.pop_front();
        // assert(iu.buf.empty());

        auto in_ch = input_channels[0];
        in_ch.get().put_credit(Credit{});

        auto src_pair = in_ch.get().src;
        dbg() << "Credit sent to {" << src_pair.first << ", " << src_pair.second
              << "}\n";

        // Self-tick autonomously unless all input ports are empty.
        mark_reschedule();
    }
}

void Router::fetch_flit() {
    for (int iport = 0; iport < get_radix(); iport++) {
        auto &ich = input_channels[iport].get();
        auto &iu = input_units[iport];
        auto flit_opt = ich.get();
        if (flit_opt) {
            dbg() << "Fetched flit " << flit_opt.value()
                  << ", buf.size()=" << iu.buf.size() << std::endl;

            // If the buffer was empty, this is the only place to kickstart the
            // pipeline.
            if (iu.buf.empty()) {
                // If the input unit state was also idle (empty != idle!), set
                // the stage to RC.
                if (iu.stage == PipelineStage::Idle) {
                    assert(iu.state.global ==
                           InputUnit::State::GlobalState::Idle);

                    // Idle -> RC transition
                    iu.state.global = InputUnit::State::GlobalState::Routing;
                    iu.stage = PipelineStage::RC;
                }

                mark_reschedule();
            }

            iu.buf.push_back(flit_opt.value());

            assert(iu.buf.size() <= input_buf_size && "Input buffer overflow!");
        }
    }
}

void Router::fetch_credit() {
    for (int oport = 0; oport < get_radix(); oport++) {
        auto &ou = output_units[oport];
        auto &och = output_channels[oport].get();
        auto credit_opt = och.get_credit();

        if (credit_opt) {
            dbg() << "Fetched credit, oport=" << oport << std::endl;
            ou.buf_credit = credit_opt.value();
            mark_reschedule();
        }
    }
}

void Router::credit_update() {
    for (int oport = 0; oport < get_radix(); oport++) {
        auto &ou = output_units[oport];
        if (ou.buf_credit) {
            dbg() << "Credit update! credit=" << ou.state.credit_count << "->"
                  << ou.state.credit_count + 1 << " (oport=" << oport << ")\n";
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
                mark_reschedule();
                dbg() << "credit update with kickstart! (iport="
                      << ou.state.input_port << ")\n";
            } else if (ou.state.credit_count == 1) {
                // XXX: This is for waking up the source node, but is it
                // necessary for other types of node?
                mark_reschedule();
                dbg() << "credit update with kickstart! (iport="
                      << ou.state.input_port << ")\n";
            } else {
                dbg() << "credit update, but no kickstart\n";
            }
        } else {
            // dbg() << "No credit update, oport=" << oport << std::endl;
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
                int total = 4;
                int cw_dist =
                    (flit.route_info.dst - flit.route_info.src + total) % total;
                if (cw_dist < total / 2) {
                    // Clockwise is better
                    iu.state.route_port = 2;
                } else {
                    // TODO: if CW == CCW, pick random
                    iu.state.route_port = 1;
                }
            }

            dbg() << flit << " RC success (port=" << iu.state.route_port << ")\n";

            // RC -> VA transition
            iu.state.global = InputUnit::State::GlobalState::VCWait;
            iu.stage = PipelineStage::VA;
            mark_reschedule();
        }
    }
}

// This function expects the given output VC to be in the Idle state.
int Router::vc_arbit_round_robin(int out_port) {
    int iport = (last_grant_input + 1) % get_radix();

    for (int i = 0; i < get_radix(); i++) {
        auto &iu = input_units[iport];

        if (iu.stage == PipelineStage::VA && iu.state.route_port == out_port) {
            // XXX: is VA stage and VCWait state the same?
            assert(iu.state.global == InputUnit::State::GlobalState::VCWait);
            dbg() << "granted oport " << out_port << " to iport " << iport << std::endl;
            last_grant_input = iport;
            return iport;
        }

        iport = (iport + 1) % get_radix();
    }

    // Indicates that there was no request for this VC.
    return -1;
}

void Router::vc_alloc() {
    // dbg() << "VC allocation\n";

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
                mark_reschedule();

                dbg() << "VA success for " << iu.buf.front() << std::endl;
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
                // their body flits have not yet arrived.  Check that.
                if (iu.buf.empty()) {
                    continue;
                }

                Flit flit = iu.buf.front();
                dbg() << flit << " switch allocation\n";

                // Check credit first; if no credit available, switch to
                // CreditWait state and do not attempt allocation at all.
                if (ou.state.credit_count <= 0) {
                    dbg() << "Credit stall! port=" << iu.state.route_port
                          << std::endl;
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
                    mark_reschedule();

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
    for (int iport = 0; iport < get_radix(); iport++) {
        auto &iu = input_units[iport];
        auto &ou = output_units[iu.state.route_port];

        if (iu.stage == PipelineStage::ST) {
            dbg() << iu.buf.front() << " switch traverse\n";
            assert(!iu.buf.empty());
            assert(iu.state.global == InputUnit::State::GlobalState::Active);
            assert(ou.state.global == OutputUnit::State::GlobalState::Active);

            Flit flit = iu.buf.front();
            iu.buf.pop_front();

            // No output speedup: there is no need for an output buffer
            // (Ch17.3).  Flits that exit the switch are directly placed on the
            // channel.
            auto out_ch = output_channels[iu.state.route_port];
            out_ch.get().put(flit);
            auto dst_pair = out_ch.get().dst;
            dbg() << "Flit " << flit << " sent to {" << dst_pair.first << ", "
                  << dst_pair.second << "}\n";

            // With output speedup:
            // auto &ou = output_units[iu.state.route_port];
            // ou.buf.push_back(flit);

            // CT stage: return credit to the upstream node.
            auto in_ch = input_channels[iport];
            in_ch.get().put_credit(Credit{});
            auto src_pair = in_ch.get().src;
            dbg() << "Credit sent to {" << src_pair.first << ", "
                  << src_pair.second << "}\n";

            // ST -> ?? transition
            // TODO: if tail flit, switch global state to idle.
            if (flit.type == Flit::Type::Tail) {
                ou.state.global = OutputUnit::State::GlobalState::Idle;
                if (iu.buf.empty()) {
                    iu.state.global = InputUnit::State::GlobalState::Idle;
                    iu.stage = PipelineStage::Idle;
                } else {
                    iu.state.global = InputUnit::State::GlobalState::Routing;
                    iu.stage = PipelineStage::RC;
                }
                mark_reschedule();
            } else {
                iu.state.global = InputUnit::State::GlobalState::Active;
                iu.stage = PipelineStage::SA;
                mark_reschedule();
            }
        }
    }
}
