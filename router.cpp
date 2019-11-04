#include "router.h"
#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"
#include <cassert>
#include <iomanip>
#include <iostream>
#include <random>

static Event tick_event_from_id(Id id) {
    return Event{id, [](Router &r) { r.tick(); }};
}

Channel::Channel(EventQueue &eq, const long dl, const RouterPortPair s,
                 const RouterPortPair d)
    : src(s), dst(d), eventq(eq), delay(dl)
{
}

void Channel::put(const Flit &flit) {
    buf.push_back({eventq.curr_time() + delay, flit});
    eventq.reschedule(delay, tick_event_from_id(dst.id));
}

void Channel::put_credit(const Credit &credit) {
    buf_credit.push_back({eventq.curr_time() + delay, credit});
    eventq.reschedule(delay, tick_event_from_id(src.id));
}

std::optional<Flit> Channel::get()
{
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

Topology topology_create(void) {
  return (Topology){
      .forward_hash = NULL,
      .reverse_hash = NULL,
  };
}

void topology_destroy(Topology *top)
{
  hmfree(top->forward_hash);
  hmfree(top->reverse_hash);
}

bool topology_connect(Topology *t, RouterPortPair input, RouterPortPair output)
{
  size_t inkey = RPHASH(&input);
  size_t outkey = RPHASH(&output);
  if (hmgeti(t->forward_hash, inkey) != -1 || hmgeti(t->reverse_hash, outkey) != -1) {
    // Bad connectivity: source or destination port is already connected
    return false;
  }
  Connection conn = (Connection){.src = input, .dst = output};
  hmput(t->forward_hash, inkey, conn);
  hmput(t->reverse_hash, outkey, conn);
  assert(hmgeti(t->forward_hash, inkey) != -1);
  return true;
}

bool topology_connect_terminals(Topology *t, const int *ids) {
    bool res = true;

    for (int i = 0; i < arrlen(ids); i++) {
      RouterPortPair src_port{src_id(ids[i]), 0};
      RouterPortPair dst_port{dst_id(ids[i]), 0};
      RouterPortPair rtr_port{rtr_id(ids[i]), 0};

      // Bidirectional channel
      res &= topology_connect(t, src_port, rtr_port);
      res &= topology_connect(t, rtr_port, dst_port);
      if (!res)
        return false;
    }

    return true;
}

// Port usage: 0:terminal, 1:counter-clockwise, 2:clockwise
bool topology_connect_ring(Topology *t, const int *ids) {
    bool res = true;

    for (long i = 0; i < arrlen(ids); i++) {
        int l = ids[i];
        int r = ids[(i + 1) % arrlen(ids)];
        RouterPortPair lport{rtr_id(l), 2};
        RouterPortPair rport{rtr_id(r), 1};

        // Bidirectional channel
        res &= topology_connect(t, lport, rport);
        res &= topology_connect(t, rport, lport);
        if (!res) return false;
    }

    return true;
}

Topology topology_ring(int n)
{
  Topology top = topology_create();
  int *ids = NULL;
  bool res = true;

  for (int id = 0; id < n; id++)
    arrput(ids, id);

  // Inter-router channels
  res &= topology_connect_ring(&top, ids);
  // Terminal node channels
  res &= topology_connect_terminals(&top, ids);
  assert(res);

  arrfree(ids);
  return top;
}

Connection conn_find_forward(Topology *t, RouterPortPair out_port)
{
  size_t key = RPHASH(&out_port);
  ptrdiff_t idx = hmgeti(t->forward_hash, key);
  if (idx == -1)
    return not_connected;
  else
    return t->forward_hash[idx].value;
}

Connection conn_find_reverse(Topology *t, RouterPortPair in_port)
{
  size_t key = RPHASH(&in_port);
  ptrdiff_t idx = hmgeti(t->reverse_hash, key);
  if (idx == -1)
    return not_connected;
  else
    return t->reverse_hash[idx].value;
}

std::ostream &operator<<(std::ostream &out, const Flit &flit)
{
  out << "{" << flit.route_info.src << "." << flit.payload << "}";
  return out;
}

static InputUnit input_unit_create(void) {
  return (InputUnit){
      .global = STATE_IDLE,
      .next_global = STATE_IDLE,
      .route_port = -1,
      .output_vc = 0,
      .stage = PIPELINE_IDLE,
      .buf = std::deque<Flit>{},
      .st_ready = std::optional<Flit>{},
  };
}

static OutputUnit output_unit_create(int bufsize) {
  return (OutputUnit){
      .global = STATE_IDLE,
      .next_global = STATE_IDLE,
      .input_port = -1,
      .input_vc = 0,
      .credit_count = bufsize,
      .buf_credit = std::optional<Credit>{},
  };
}

Router::Router(EventQueue &eq, Stat &st, TopoDesc td, Id id_, int radix,
               const std::vector<Channel *> &in_chs,
               const std::vector<Channel *> &out_chs)
    : id(id_), eventq(eq), stat(st), top_desc(td),
      tick_event(tick_event_from_id(id_)), input_channels(in_chs),
      output_channels(out_chs)
{
  // input_units = NULL;
  // output_units = NULL;
  for (int port = 0; port < radix; port++) {
    InputUnit iu = input_unit_create();
    OutputUnit ou = output_unit_create(input_buf_size);
    input_units.push_back(iu);
    output_units.push_back(ou);
  }

  if (is_src(id) || is_dst(id)) {
    assert(input_units.size() == 1);
    assert(output_units.size() == 1);
    input_units[0].route_port = 0;
    output_units[0].input_port = 0;
  }

  std::random_device rd;
  std::mt19937 gen{rd()};
  std::uniform_int_distribution<> dist{0, get_radix() - 1};
  va_last_grant_input = dist(gen);
  va_last_grant_input = 0;
  sa_last_grant_input = dist(gen);
  sa_last_grant_input = 0;
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

std::vector<int> source_route_compute(TopoDesc td, int src_id, int dst_id) {
    std::vector<int> path;

    int total = td.k;
    int cw_dist = (dst_id - src_id + total) % total;
    if (cw_dist <= total / 2) {
        // Clockwise
        for (int i = 0; i < cw_dist; i++) {
            path.push_back(2);
        }
        path.push_back(0);
    } else {
        // Counterclockwise
        // TODO: if CW == CCW, pick random
        for (int i = 0; i < total - cw_dist; i++) {
            path.push_back(1);
        }
        path.push_back(0);
    }

    std::cout << "Source route computation: " << src_id << " -> " << dst_id
              << ": {";
    for (auto i : path) {
        std::cout << i << ",";
    }
    std::cout << "}\n";

    return path;
}

void Router::tick() {
    // Make sure this router has not been already ticked in this cycle.
    if (eventq.curr_time() == last_tick) {
        // dbg() << "WARN: double tick! curr_time=" << eventq.curr_time()
        //       << ", last_tick=" << last_tick << std::endl;
        stat.double_tick_count++;
        return;
    }
    // assert(eventq.curr_time() != last_tick);

    reschedule_next_tick = false;

    // Different tick actions for different types of node.
    if (is_src(id)) {
        source_generate();
        // Source nodes also needs to manage credit in order to send flits at
        // the right time.
        credit_update();
        fetch_credit();
    } else if (is_dst(id)) {
        destination_consume();
        fetch_flit();
    } else {
        // Process each pipeline stage.
        // Stages are processed in reverse dependency order to prevent coherence
        // bug.  E.g., if a flit succeeds in route_compute() and advances to the
        // VA stage, and then vc_alloc() is called, it would then get processed
        // again in the same cycle.
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
    OutputUnit *ou = &output_units[0];

    if (ou->credit_count <= 0) {
        dbg() << "Credit stall!\n";
        return;
    }

    Flit flit{FLIT_BODY, id.value,
              (id.value + 2) % 4, flit_payload_counter};
    if (flit_payload_counter == 0) {
        flit.type = FLIT_HEAD ;
        flit.route_info.path = source_route_compute(
            top_desc, flit.route_info.src, flit.route_info.dst);
        flit_payload_counter++;
    } else if (flit_payload_counter == 3 /* FIXME */) {
        flit.type = FLIT_TAIL;
        flit_payload_counter = 0;
    } else {
        flit_payload_counter++;
    }

    assert(get_radix() == 1);
    Channel *out_ch = output_channels[0];
    out_ch->put(flit);

    dbg() << "Credit decrement, credit=" << ou->credit_count << "->"
          << ou->credit_count - 1 << ";\n";
    ou->credit_count--;
    assert(ou->credit_count >= 0);

    flit_gen_count++;
    dbg() << flit << " Flit created and sent!\n";

    // TODO: for now, infinitely generate flits.
    mark_reschedule();
}

void Router::destination_consume() {
    InputUnit *iu = &input_units[0];

    if (!iu->buf.empty()) {
        dbg() << "Destination buf size=" << iu->buf.size() << std::endl;
        dbg() << iu->buf.front() << " Flit arrived!\n";

        flit_arrive_count++;
        iu->buf.pop_front();
        // assert(iu->buf.empty());

        Channel *in_ch = input_channels[0];
        in_ch->put_credit(Credit{});

        auto src_pair = in_ch->src;
        dbg() << "Credit sent to {" << src_pair.id << ", " << src_pair.port
              << "}\n";

        // Self-tick autonomously unless all input ports are empty.
        mark_reschedule();
    }
}

void Router::fetch_flit() {
    for (int iport = 0; iport < get_radix(); iport++) {
        Channel *ich = input_channels[iport];
        InputUnit *iu = &input_units[iport];
        auto flit_opt = ich->get();

        if (flit_opt) {
            dbg() << "Fetched flit " << flit_opt.value()
                  << ", buf.size()=" << iu->buf.size() << std::endl;

            // If the buffer was empty, this is the only place to kickstart the
            // pipeline.
            if (iu->buf.empty()) {
                dbg() << "fetch_flit: buf was empty\n";
                // If the input unit state was also idle (empty != idle!), set
                // the stage to RC.
                if (iu->next_global == STATE_IDLE) {
                    // Idle -> RC transition
                    iu->next_global = STATE_ROUTING;
                    iu->stage = PIPELINE_RC;
                }

                mark_reschedule();
            }

            iu->buf.push_back(flit_opt.value());

            assert(iu->buf.size() <= input_buf_size && "Input buffer overflow!");
        }
    }
}

void Router::fetch_credit() {
    for (int oport = 0; oport < get_radix(); oport++) {
        OutputUnit *ou = &output_units[oport];
        Channel *och = output_channels[oport];
        auto credit_opt = och->get_credit();

        if (credit_opt) {
            dbg() << "Fetched credit, oport=" << oport << std::endl;
            ou->buf_credit = credit_opt.value();
            mark_reschedule();
        }
    }
}

void Router::credit_update() {
    for (int oport = 0; oport < get_radix(); oport++) {
        OutputUnit *ou = &output_units[oport];
        if (ou->buf_credit) {
            dbg() << "Credit update! credit=" << ou->credit_count << "->"
                  << ou->credit_count + 1 << " (oport=" << oport << ")\n";
            // Upon credit update, the input and output unit receiving this
            // credit may or may not be in the CreditWait state.  If they are,
            // make sure to switch them back to the active state so that they
            // can proceed in the SA stage.
            //
            // This can otherwise be implemented in the SA stage itself,
            // switching the stage to Active and simultaneously commencing to
            // the switch allocation.  However, this implementation seems to
            // defeat the purpose of the CreditWait stage. This implementation
            // is what I think of as a more natural one.
            assert(ou->input_port != -1); // XXX: redundant?
            InputUnit *iu = &input_units[ou->input_port];
            if (ou->credit_count == 0) {
                if (ou->next_global == STATE_CREDWAIT) {
                    assert(iu->next_global == STATE_CREDWAIT);
                    iu->next_global = STATE_ACTIVE;
                    ou->next_global = STATE_ACTIVE;
                }
                mark_reschedule();
                dbg() << "credit update with kickstart! (iport="
                      << ou->input_port << ")\n";
            } else {
                dbg() << "credit update, but no kickstart (credit="
                      << ou->credit_count << ")\n";
            }

            ou->credit_count++;
            ou->buf_credit.reset();
        } else {
            // dbg() << "No credit update, oport=" << oport << std::endl;
        }
    }
}

void Router::route_compute() {
    for (int port = 0; port < get_radix(); port++) {
        InputUnit *iu = &input_units[port];

        if (iu->global == STATE_ROUTING) {
            auto &flit = iu->buf.front();
            dbg() << flit << " route computation\n";
            assert(!iu->buf.empty());

            // TODO: Simple algorithmic routing: keep rotating clockwise until
            // destination is met.
            // if (flit.route_info.dst == std::get<RtrId>(id).id) {
            //     // Port 0 is always connected to a terminal node
            //     iu->route_port = 0;
            // } else {
            //     int total = 4; /* FIXME: hardcoded */
            //     int cw_dist =
            //         (flit.route_info.dst - flit.route_info.src + total) % total;
            //     if (cw_dist <= total / 2) {
            //         // Clockwise is better
            //         iu->route_port = 2;
            //     } else {
            //         // TODO: if CW == CCW, pick random
            //         iu->route_port = 1;
            //     }
            // }

            assert(flit.route_info.idx < flit.route_info.path.size());
            dbg() << "RC: path size = " << flit.route_info.path.size() << std::endl;
            iu->route_port = flit.route_info.path[flit.route_info.idx];
            dbg() << flit << " RC success (idx=" << flit.route_info.idx
                  << ", oport=" << iu->route_port << ")\n";
            flit.route_info.idx++;

            // RC -> VA transition
            iu->next_global = STATE_VCWAIT;
            iu->stage = PIPELINE_VA;
            mark_reschedule();
        }
    }
}

// This function expects the given output VC to be in the Idle state.
int Router::vc_arbit_round_robin(int out_port)
{
  int iport = (va_last_grant_input + 1) % get_radix();

  int *v = NULL;
  for (int i = 0; i < get_radix(); i++) {
    InputUnit *iu = &input_units[i];
    if (iu->global == STATE_VCWAIT && iu->route_port == out_port)
      arrput(v, i);
  }
  if (arrlen(v)) {
    dbg() << "VA: competing for oport " << out_port << " from iports {";
    for (int i = 0; i < arrlen(v); i++)
      std::cout << v[i] << ",";
    std::cout << "}\n";
  }
  arrfree(v);

  for (int i = 0; i < get_radix(); i++) {
    InputUnit *iu = &input_units[iport];

    if (iu->global == STATE_VCWAIT && iu->route_port == out_port) {
      // XXX: is VA stage and VCWait state the same?
      assert(iu->stage == PIPELINE_VA);
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
        InputUnit *iu = &input_units[iport];

        if (iu->stage == PIPELINE_SA && iu->route_port == out_port &&
            iu->global == STATE_ACTIVE) {
            // dbg() << "SA: granted oport " << out_port << " to iport " << iport
            //       << std::endl;
            sa_last_grant_input = iport;
            return iport;
        } else if (iu->stage == PIPELINE_SA && iu->route_port == out_port &&
                   iu->global == STATE_CREDWAIT) {
            dbg() << "Credit stall! port=" << iu->route_port << std::endl;
        }

        iport = (iport + 1) % get_radix();
    }

    // Indicates that there was no request for this VC.
    return -1;
}

void Router::vc_alloc() {
    // dbg() << "VC allocation\n";

    for (int oport = 0; oport < get_radix(); oport++) {
        OutputUnit *ou = &output_units[oport];

        // Only do arbitration for inactive output VCs.
        if (ou->global == STATE_IDLE) {
            // Arbitration
            int iport = vc_arbit_round_robin(oport);

            if (iport == -1) {
                // dbg() << "no pending VC request for oport=" << oport << std::endl;
            } else {
                InputUnit *iu = &input_units[iport];

                dbg() << "VA: success for " << iu->buf.front() << " from iport "
                      << iport << " to oport " << oport << std::endl;

                // We now have the VC, but we cannot proceed to the SA stage if
                // there is no credit.
                if (ou->credit_count == 0) {
                    dbg() << "VA: no credit, switching to CreditWait\n";
                    iu->next_global = STATE_CREDWAIT;
                    ou->next_global = STATE_CREDWAIT;
                } else {
                    iu->next_global = STATE_ACTIVE;
                    ou->next_global = STATE_ACTIVE;
                }

                // Record the input port to the Output unit.
                ou->input_port = iport;

                iu->stage = PIPELINE_SA;
                mark_reschedule();
            }
        }
    }
}

void Router::switch_alloc() {
    for (int oport = 0; oport < get_radix(); oport++) {
        OutputUnit *ou = &output_units[oport];

        // Only do arbitration for output VCs that has available credits.
        if (ou->global == STATE_ACTIVE) {
            // Arbitration
            int iport = sa_arbit_round_robin(oport);

            if (iport == -1) {
                // dbg() << "no pending SA request!\n";
            } else {
                // SA success!
                InputUnit *iu = &input_units[iport];

                dbg() << "SA success for " << iu->buf.front() << " from iport "
                      << iport << " to oport " << oport << std::endl;

                // Input units in the active state *may* be empty, e.g. if
                // their body flits have not yet arrived.  Check that.
                assert(!iu->buf.empty());
                // if (iu->buf.empty()) {
                //     continue;
                // }

                // The flit leaves the buffer here.
                Flit flit = iu->buf.front();
                dbg() << flit << " switch allocation success\n";
                assert(iu->global == STATE_ACTIVE);
                iu->buf.pop_front();

                assert(!iu->st_ready.has_value()); // XXX: harsh
                iu->st_ready = flit;

                assert(ou->global == STATE_ACTIVE);

                // Credit decrement
                dbg() << "Credit decrement, credit=" << ou->credit_count
                      << "->" << ou->credit_count - 1 << " (oport=" << oport
                      << ");\n";
                assert(ou->credit_count > 0);
                ou->credit_count--;

                // SA -> ?? transition
                //
                // Set the next stage according to the flit type and credit
                // count.
                //
                // Note that switching state to CreditWait does NOT prevent the
                // subsequent ST to happen. The flit that has succeeded SA on
                // this cycle is transferred to iu->st_ready, and that is the
                // only thing that is visible to the ST stage.
                if (flit.type == FLIT_TAIL) {
                    ou->next_global = STATE_IDLE;
                    if (iu->buf.empty()) {
                        iu->next_global = STATE_IDLE;
                        iu->stage = PIPELINE_IDLE;
                        dbg() << "SA: next state is Idle\n";
                    } else {
                        iu->next_global = STATE_ROUTING;
                        iu->stage = PIPELINE_RC;
                        dbg() << "SA: next state is Routing\n";
                    }
                    mark_reschedule();
                } else if (ou->credit_count == 0) {
                    dbg() << "SA: switching to CW\n";
                    iu->next_global = STATE_CREDWAIT;
                    ou->next_global = STATE_CREDWAIT;
                    dbg() << "SA: next state is CreditWait\n";
                } else {
                    iu->next_global = STATE_ACTIVE;
                    iu->stage = PIPELINE_SA;
                    dbg() << "SA: next state is Active\n";
                    mark_reschedule();
                }
                assert(ou->credit_count >= 0);
            }
        }
    }
}

void Router::switch_traverse() {
    for (int iport = 0; iport < get_radix(); iport++) {
        InputUnit *iu = &input_units[iport];

        if (iu->st_ready.has_value()) {
            Flit flit = iu->st_ready.value();
            iu->st_ready.reset();
            dbg() << flit << " switch traverse\n";

            // No output speedup: there is no need for an output buffer
            // (Ch17.3).  Flits that exit the switch are directly placed on the
            // channel.
            Channel *out_ch = output_channels[iu->route_port];
            out_ch->put(flit);
            auto dst_pair = out_ch->dst;
            dbg() << "Flit " << flit << " sent to {" << dst_pair.id << ", "
                  << dst_pair.port << "}\n";

            // With output speedup:
            // auto &ou = output_units[iu->route_port];
            // ou->buf.push_back(flit);

            // CT stage: return credit to the upstream node.
            Channel *in_ch = input_channels[iport];
            in_ch->put_credit(Credit{});
            auto src_pair = in_ch->src;
            dbg() << "Credit sent to {" << src_pair.id << ", "
                  << src_pair.port << "}\n";
        }
    }
}

void Router::update_states() {
    bool changed = false;

    for (int port = 0; port < get_radix(); port++) {
        InputUnit *iu = &input_units[port];
        OutputUnit *ou = &output_units[port];
        if (iu->global != iu->next_global) {
            iu->global = iu->next_global;
            changed = true;
        }
        if (ou->global != ou->next_global) {
            if (ou->next_global == STATE_CREDWAIT && ou->credit_count > 0) {
                assert(false);
            }
            ou->global = ou->next_global;
            changed = true;
        }
    }

    // Reschedule whenever there is one or more state change.
    if (changed) {
        mark_reschedule();
    }
}
