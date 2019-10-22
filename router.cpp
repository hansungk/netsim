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

Router::Router(EventQueue &eq, Stat &st, Id id_, int radix,
               const ChannelRefVec &in_chs, const ChannelRefVec &out_chs)
    : id(id_), eventq(eq), stat(st), tick_event(tick_event_from_id(id_)),
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
        stat.double_tick_count++;
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

    // Update the global state of each input/output unit.
    update_states();

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
    Flit flit{Flit::Type::Body, std::get<SrcId>(id).id,
              (std::get<SrcId>(id).id + 2) % 4, flit_payload_counter};
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
                dbg() << "fetch_flit: buf was empty\n";
                // If the input unit state was also idle (empty != idle!), set
                // the stage to RC.
                if (iu.state.next_global == InputUnit::State::GlobalState::Idle) {
                    // Idle -> RC transition
                    iu.state.next_global = InputUnit::State::GlobalState::Routing;
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
            // Upon credit update, the input and output unit receiving this
            // credit may or may not be in the CreditWait state.  If they are,
            // make sure to switch them back to the active state so that they
            // can proceed in the SA stage.
            //
            // This can otherwise be implemented in the SA stage itself,
            // switching the stage to Active and simultaneously commencing to
            // the switch allocation.  However, this implementation seems
            // to defeat the purpose of the CreditWait stage. This
            // implementation is what I think of as a more natural one.
            assert(ou.state.input_port != -1); // XXX: redundant?
            auto &iu = input_units[ou.state.input_port];
            if (ou.state.credit_count == 0) {
                if (ou.state.next_global ==
                    OutputUnit::State::GlobalState::CreditWait) {
                    assert(iu.state.next_global ==
                           InputUnit::State::GlobalState::CreditWait);
                    iu.state.next_global =
                        InputUnit::State::GlobalState::Active;
                    ou.state.next_global =
                        OutputUnit::State::GlobalState::Active;
                }
                mark_reschedule();
                dbg() << "credit update with kickstart! (iport="
                      << ou.state.input_port << ")\n";
            } else {
                dbg() << "credit update, but no kickstart (credit="
                      << ou.state.credit_count << ")\n";
            }

            ou.state.credit_count++;
            ou.buf_credit.reset();
        } else {
            // dbg() << "No credit update, oport=" << oport << std::endl;
        }
    }
}

void Router::route_compute() {
    for (int port = 0; port < get_radix(); port++) {
        auto &iu = input_units[port];

        if (iu.state.global == InputUnit::State::GlobalState::Routing) {
            auto flit = iu.buf.front();
            dbg() << flit << " route computation\n";
            assert(!iu.buf.empty());

            // TODO: Simple algorithmic routing: keep rotating clockwise until
            // destination is met.
            if (flit.route_info.dst == std::get<RtrId>(id).id) {
                // Port 0 is always connected to a terminal node
                iu.state.route_port = 0;
            } else {
                int total = 4; /* FIXME: hardcoded */
                int cw_dist =
                    (flit.route_info.dst - flit.route_info.src + total) % total;
                if (cw_dist <= total / 2) {
                    // Clockwise is better
                    iu.state.route_port = 2;
                } else {
                    // TODO: if CW == CCW, pick random
                    iu.state.route_port = 1;
                }
            }

            dbg() << flit << " RC success (oport=" << iu.state.route_port << ")\n";

            // RC -> VA transition
            iu.state.next_global = InputUnit::State::GlobalState::VCWait;
            iu.stage = PipelineStage::VA;
            mark_reschedule();
        }
    }
}

// This function expects the given output VC to be in the Idle state.
int Router::vc_arbit_round_robin(int out_port) {
    int iport = (va_last_grant_input + 1) % get_radix();

    for (int i = 0; i < get_radix(); i++) {
        auto &iu = input_units[iport];

        if (iu.state.global == InputUnit::State::GlobalState::VCWait &&
            iu.state.route_port == out_port) {
            // XXX: is VA stage and VCWait state the same?
            assert(iu.stage == PipelineStage::VA);
            dbg() << "VA: granted oport " << out_port << " to iport " << iport << std::endl;
            va_last_grant_input = iport;
            return iport;
        }

        iport = (iport + 1) % get_radix();
    }

    // Indicates that there was no request for this VC.
    return -1;
}

// This function expects the given output VC to be in the Idle state.
int Router::sa_arbit_round_robin(int out_port) {
    int iport = (sa_last_grant_input + 1) % get_radix();

    for (int i = 0; i < get_radix(); i++) {
        auto &iu = input_units[iport];

        if (iu.stage == PipelineStage::SA && iu.state.route_port == out_port &&
            iu.state.global == InputUnit::State::GlobalState::Active) {
            dbg() << "SA: granted oport " << out_port << " to iport " << iport
                  << std::endl;
            sa_last_grant_input = iport;
            return iport;
        } else if (iu.stage == PipelineStage::SA &&
                   iu.state.route_port == out_port &&
                   iu.state.global ==
                       InputUnit::State::GlobalState::CreditWait) {
            dbg() << "Credit stall! port=" << iu.state.route_port << std::endl;
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

                // We now have the VC, but we cannot proceed to the SA stage if
                // there is no credit.
                if (ou.state.credit_count == 0) {
                    dbg() << "VA: switching to CW\n";
                    iu.state.next_global =
                        InputUnit::State::GlobalState::CreditWait;
                    ou.state.next_global =
                        OutputUnit::State::GlobalState::CreditWait;
                } else {
                    iu.state.next_global =
                        InputUnit::State::GlobalState::Active;
                    ou.state.next_global =
                        OutputUnit::State::GlobalState::Active;
                }

                // Record the input port to the Output unit.
                ou.state.input_port = iport;

                iu.stage = PipelineStage::SA;
                mark_reschedule();

                dbg() << "VA success for " << iu.buf.front() << " to port " << oport << std::endl;
            }
        }
    }
}

void Router::switch_alloc() {
    for (int oport = 0; oport < get_radix(); oport++) {
        auto &ou = output_units[oport];

        // Only do arbitration for output VCs that has available credits.
        if (ou.state.global == OutputUnit::State::GlobalState::Active) {
            // Arbitration
            int iport = sa_arbit_round_robin(oport);

            if (iport == -1) {
                // dbg() << "no pending SA request!\n";
            } else {
                // SA success!
                auto &iu = input_units[iport];

                // Input units in the active state *may* be empty, e.g. if
                // their body flits have not yet arrived.  Check that.
                assert(!iu.buf.empty());
                // if (iu.buf.empty()) {
                //     continue;
                // }

                // The flit leaves the buffer here.
                Flit flit = iu.buf.front();
                dbg() << flit << " switch allocation success\n";
                assert(iu.state.global == InputUnit::State::GlobalState::Active);
                iu.buf.pop_front();

                assert(!iu.st_ready.has_value()); // XXX: harsh
                iu.st_ready = flit;

                assert(ou.state.global ==
                       OutputUnit::State::GlobalState::Active);

                // Credit decrement
                dbg() << "Credit decrement, credit=" << ou.state.credit_count
                      << "->" << ou.state.credit_count - 1 << " (oport=" << oport
                      << ");\n";
                assert(ou.state.credit_count > 0);
                ou.state.credit_count--;

                // SA -> ?? transition
                //
                // Set the next stage according to the flit type and credit count.
                //
                // Note that switching state to CreditWait does NOT prevent the
                // subsequent ST to happen. The flit that has succeeded SA on
                // this cycle is transferred to iu.st_ready, and that is the
                // only thing that is visible to the ST stage.
                if (flit.type == Flit::Type::Tail) {
                    ou.state.next_global = OutputUnit::State::GlobalState::Idle;
                    if (iu.buf.empty()) {
                        iu.state.next_global =
                            InputUnit::State::GlobalState::Idle;
                        iu.stage = PipelineStage::Idle;
                        // dbg() << "SA: next state is Idle\n";
                    } else {
                        iu.state.next_global =
                            InputUnit::State::GlobalState::Routing;
                        iu.stage = PipelineStage::RC;
                        // dbg() << "SA: next state is Routing\n";
                    }
                    mark_reschedule();
                } else if (ou.state.credit_count == 0) {
                    // dbg() << "SA: switching to CW\n";
                    iu.state.next_global =
                        InputUnit::State::GlobalState::CreditWait;
                    ou.state.next_global =
                        OutputUnit::State::GlobalState::CreditWait;
                    // dbg() << "SA: next state is CreditWait\n";
                    mark_reschedule();
                } else {
                    iu.state.next_global =
                        InputUnit::State::GlobalState::Active;
                    iu.stage = PipelineStage::SA;
                    // dbg() << "SA: next state is Active\n";
                    mark_reschedule();
                }
                assert(ou.state.credit_count >= 0);
            }
        }
    }
}

void Router::switch_traverse() {
    for (int iport = 0; iport < get_radix(); iport++) {
        auto &iu = input_units[iport];

        if (iu.st_ready.has_value()) {
            Flit flit = iu.st_ready.value();
            iu.st_ready.reset();
            dbg() << flit << " switch traverse\n";

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
        }
    }
}

void Router::update_states() {
    bool changed = false;

    for (int port = 0; port < get_radix(); port++) {
        auto &iu = input_units[port];
        auto &ou = output_units[port];
        if (iu.state.global != iu.state.next_global) {
            iu.state.global = iu.state.next_global;
            changed = true;
        }
        if (ou.state.global != ou.state.next_global) {
            if (ou.state.next_global ==
                    OutputUnit::State::GlobalState::CreditWait &&
                ou.state.credit_count > 0) {
                assert(false);
            }
            ou.state.global = ou.state.next_global;
            changed = true;
        }
    }

    // Reschedule whenever there is one or more state change.
    if (changed) {
        mark_reschedule();
    }
}
