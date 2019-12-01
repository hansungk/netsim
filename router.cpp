#include "router.h"
#include "sim.h"
#include "queue.h"
#include "stb_ds.h"
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
#include <random>

TrafficDesc::TrafficDesc(int terminal_count)
    : type(TRF_UNIFORM_RANDOM), dests(terminal_count)
{
}

RandomGenerator::RandomGenerator(int terminal_count)
    : def(), rd(), uni_dist(0, terminal_count - 1)
{
    // TODO: seed?
}

void debugf(Router *r, const char *fmt, ...)
{
    char s[IDSTRLEN];
    printf("[@%3ld] [%s] ", curr_time(r->eventq), id_str(r->id, s));
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

Event tick_event_from_id(Id id)
{
    return (Event){id, router_tick};
}

Channel::Channel(EventQueue *eq, long dl, const Connection conn)
    : conn(conn), eventq(eq), delay(dl), buf_credit()
{
    queue_init(buf, dl + CHANNEL_SLACK);
}

Channel::~Channel()
{
    queue_free(buf);
}

void channel_put(Channel *ch, Flit *flit)
{
    TimedFlit tf = {curr_time(ch->eventq) + ch->delay, flit};
    assert(!queue_full(ch->buf));
    queue_put(ch->buf, tf);
    reschedule(ch->eventq, ch->delay, tick_event_from_id(ch->conn.dst.id));
}

void channel_put_credit(Channel *ch, Credit *credit)
{
    TimedCredit tc = {curr_time(ch->eventq) + ch->delay, credit};
    ch->buf_credit.push_back(tc);
    reschedule(ch->eventq, ch->delay, tick_event_from_id(ch->conn.src.id));
}

Flit *channel_get(Channel *ch)
{
    TimedFlit front = queue_front(ch->buf);
    if (!queue_empty(ch->buf) && curr_time(ch->eventq) >= front.time) {
        assert(curr_time(ch->eventq) == front.time && "stale flit!");
        Flit *flit = front.flit;
        queue_pop(ch->buf);
        return flit;
    } else {
        return NULL;
    }
}

Credit *channel_get_credit(Channel *ch)
{
    TimedCredit front = ch->buf_credit.front();
    if (!ch->buf_credit.empty() && curr_time(ch->eventq) >= front.time) {
        assert(curr_time(ch->eventq) == front.time && "stale flit!");
        Credit *credit = front.credit;
        ch->buf_credit.pop_front();
        return credit;
    } else {
        return NULL;
    }
}

void print_conn(const char *name, Connection conn)
{
    printf("%s: %d.%d.%d -> %d.%d.%d\n", name, conn.src.id.type,
           conn.src.id.value, conn.src.port, conn.dst.id.type,
           conn.dst.id.value, conn.dst.port);
}

Connection conn_find_forward(Topology *t, RouterPortPair out_port)
{
    ptrdiff_t idx = hmgeti(t->forward_hash, out_port);
    if (idx == -1)
        return not_connected;
    else
        return t->forward_hash[idx].value;
}

Connection conn_find_reverse(Topology *t, RouterPortPair in_port)
{
    ptrdiff_t idx = hmgeti(t->reverse_hash, in_port);
    if (idx == -1)
        return not_connected;
    else
        return t->reverse_hash[idx].value;
}

Flit::Flit(enum FlitType t, int vc, int src, int dst, PacketId pid, long flitnum)
    : type(t), vc_num(vc), packet_id(pid), flitnum(flitnum)
{
    route_info.src = src;
    route_info.dst = dst;
}

void flit_destroy(Flit *flit)
{
    free(flit);
}

// 's' should be at least IDSTRLEN large.
char *flit_str(const Flit *flit, char *s)
{
    // FIXME: Rename IDSTRLEN!
    if (flit) {
        snprintf(s, IDSTRLEN, "{s%d.p%ld.f%ld}", flit->route_info.src,
                 flit->packet_id.id, flit->flitnum);
    } else {
        *s = '\0';
    }
    return s;
}

char *globalstate_str(enum GlobalState state, char *s)
{
    switch (state) {
    case STATE_IDLE:
        snprintf(s, IDSTRLEN, "I");
        break;
    case STATE_ROUTING:
        snprintf(s, IDSTRLEN, "R");
        break;
    case STATE_VCWAIT:
        snprintf(s, IDSTRLEN, "V");
        break;
    case STATE_ACTIVE:
        snprintf(s, IDSTRLEN, "A");
        break;
    case STATE_CREDWAIT:
        snprintf(s, IDSTRLEN, "C");
        break;
    }
    return s;
}

InputUnit::InputUnit(int vc_count, int bufsize)
{
    vcs.reserve(vc_count);
    for (int i = 0; i < vc_count; i++) {
        vcs.emplace_back(bufsize);
    }
}

InputUnit::VC::VC(int bufsize)
{
    buf = NULL;
    queue_init(buf, bufsize * 2);
}

InputUnit::VC::~VC()
{
    queue_free(buf);
}

OutputUnit::OutputUnit(int vc_count, int bufsize)
{
    vcs.reserve(vc_count);
    for (int i = 0; i < vc_count; i++) {
        vcs.emplace_back(bufsize);
    }
}

OutputUnit::VC::VC(int bufsize) : credit_count(bufsize)
{
    buf_credit = NULL;
    queue_init(buf_credit, bufsize * 2); // FIXME: unnecessarily big.
}

OutputUnit::VC::~VC()
{
    queue_free(buf_credit);
}

Router::Router(Sim &sim, EventQueue *eq, Stat *st, Id id, int radix,
               int vc_count, TopoDesc td, TrafficDesc trd, RandomGenerator &rg,
               long packet_len, Channel **in_chs, Channel **out_chs,
               long input_buf_size)
    : sim(sim), eventq(eq), stat(st), id(id), radix(radix), vc_count(vc_count),
      top_desc(td), traffic_desc(trd), rand_gen(rg), packet_len(packet_len),
      input_buf_size(input_buf_size), src_last_grant_output(0),
      dst_last_grant_input(0), va_last_grant_input(radix * vc_count, 0),
      va_last_grant_output(radix * vc_count, 0),
      sa_last_grant_input(radix * vc_count, 0), sa_last_grant_output(radix, 0)
{
    // Copy channel list
    input_channels = NULL;
    output_channels = NULL;
    for (long i = 0; i < arrlen(in_chs); i++)
        arrput(input_channels, in_chs[i]);
    for (long i = 0; i < arrlen(out_chs); i++)
        arrput(output_channels, out_chs[i]);

    // Source queues are supposed to be infinite in size, but since our
    // queue implementation does not support dynamic extension, let's just
    // assume a fixed, arbitrary massive size for its queue. Nonurgent TODO.
    source_queue = NULL;
    if (is_src(id)) {
        queue_init(source_queue, 10000);
    }

    input_units.reserve(radix);
    output_units.reserve(radix);
    for (int port = 0; port < radix; port++) {
        input_units.emplace_back(vc_count, input_buf_size);
        output_units.emplace_back(vc_count, input_buf_size);
    }

    if (is_src(id) || is_dst(id)) {
        assert(input_units.size() == 1);
        assert(output_units.size() == 1);
        // There are no route computation stages for terminal nodes, so set the
        // routed ports and allocated VCs for each IU/OU statically here.
        for (int i = 0; i < vc_count; i++) {
            input_units[0].vcs[i].route_port = TERMINAL_PORT;
            input_units[0].vcs[i].output_vc = 0; // unnecessary?
            output_units[0].vcs[i].input_port = TERMINAL_PORT;
            output_units[0].vcs[i].input_vc = 0; // unnnecessary
        }
    }
}

Router::~Router()
{
    if (source_queue) queue_free(source_queue);
    arrfree(input_channels);
    arrfree(output_channels);
}

void router_reschedule(Router *r)
{
    if (r->reschedule_next_tick) {
        reschedule(r->eventq, 1, tick_event_from_id(r->id));
    }
}

// Compute route on a ring that is laid along a single dimension.
// Expects that src_id and dst_id is on the same ring.
// Appends computed route after 'path'. Does NOT put the final routing to the
// terminal node.
static void source_route_compute_dimension(Router *r, TopoDesc td,
                                           int src_id, int dst_id,
                                           int direction,
                                           std::vector<int> &path)
{
    int total = td.k;
    int src_id_xyz = torus_id_xyz_get(src_id, td.k, direction);
    int dst_id_xyz = torus_id_xyz_get(dst_id, td.k, direction);
    int cw_dist = (dst_id_xyz - src_id_xyz + total) % total;

    if ((total % 2) == 0 && cw_dist == (total / 2)) {
        int dice = r->rand_gen.uni_dist(r->rand_gen.def);
        int to_larger = (dice % 2 == 0) ? 1 : 0;

        // Adaptive routing

        // int first_hop_id = r->output_channels[TERMINAL_PORT]->conn.dst.id.value;
        // Router *first_hop = r->sim.routers[first_hop_id].get();
        // int credit_for_larger =
        //     first_hop->output_units[get_output_port(direction, 1)].credit_count;
        // int credit_for_smaller =
        //     first_hop->output_units[get_output_port(direction, 0)].credit_count;
        // // printf("credit_for_larger=%d, smaller=%d\n", credit_for_larger,
        // //        credit_for_smaller);

        // to_larger = 1;
        // if (credit_for_smaller > credit_for_larger) {
        //     to_larger = 0;
        // }
        // printf("adaptive routed to %d\n", get_output_port(direction, to_larger));


        // FIXME
        to_larger = 1;

        for (int i = 0; i < cw_dist; i++) {
            path.push_back(get_output_port(direction, to_larger));
        }
    } else if (cw_dist <= (total / 2)) {
        // Clockwise
        for (int i = 0; i < cw_dist; i++) {
            path.push_back(get_output_port(direction, 1));
        }
    } else {
        // Counterclockwise
        // TODO: if CW == CCW, pick random
        for (int i = 0; i < total - cw_dist; i++) {
            path.push_back(get_output_port(direction, 0));
        }
    }
}

// Source-side all-in-one route computation.
// Returns an stb array containing the series of routed output ports.
std::vector<int> source_route_compute(Router *r, TopoDesc td, int src_id, int dst_id)
{
    std::vector<int> path{};

    // Dimension-order routing. Order is XYZ.
    int last_src_id = src_id;
    for (int dir = 0; dir < td.r; dir++) {
        int interim_id = torus_align_id(td.k, last_src_id, dst_id, dir);
        // printf("%s: from %d to %d\n", __func__, last_src_id, interim_id);
        source_route_compute_dimension(r, td, last_src_id, interim_id, dir, path);
        last_src_id = interim_id;
    }
    // Enter the final destination node.
    path.push_back(TERMINAL_PORT);

    return path;
}

// Tick a router. This function does all of the work that a router has to
// process in a single cycle, i.e. all pipeline stages and statistics update.
// This simplifies the event system by streamlining event types into a single
// one, the 'tick event', and letting us to only have to consider the
// chronological order between them.
void router_tick(Router *r)
{
    // Make sure this router has not been already ticked in this cycle.
    if (curr_time(r->eventq) == r->last_tick) {
        // debugf(r, "WARN: double tick! curr_time=%ld, last_tick=%ld\n",
        //        curr_time(r->eventq), r->last_tick);
        r->stat->double_tick_count++;
        return;
    }

    r->reschedule_next_tick = false;

    // Different tick actions for different types of node.
    if (is_src(r->id)) {
        source_generate(r);
        // Source nodes also needs to manage credit in order to send flits at
        // the right time.
        credit_update(r);
        fetch_credit(r);
    } else if (is_dst(r->id)) {
        destination_consume(r);
        fetch_flit(r);
    } else {
        // Process each pipeline stage.
        // Stages are processed in reverse dependency order to prevent coherence
        // bug.  E.g., if a flit succeeds in route_compute() and advances to the
        // VA stage, and then vc_alloc() is called, it would then get processed
        // again in the same cycle.
        switch_traverse(r);
        switch_alloc(r);
        vc_alloc(r);
        route_compute(r);
        credit_update(r);
        fetch_credit(r);
        fetch_flit(r);

        // Self-tick autonomously unless all input ports are empty.
        // FIXME: redundant?
        // int empty = 1;
        // for (int i = 0; i < radix; i++) {
        //     if (!input_units[i].buf.empty()) {
        //         empty = 0;
        //         break;
        //     }
        // }
        // if (!empty) {
        //     reschedule_next_tick = 1;
        // }
    }

    // Update the global state of each input/output unit.
    update_states(r);

    // Do the rescheduling at here once to prevent flooding the event queue.
    router_reschedule(r);

    r->last_tick = curr_time(r->eventq);
}

///
/// Pipeline stages
///

void source_generate(Router *r)
{
    // Before entering the source queue.
    if (!queue_full(r->source_queue) &&
        (r->eventq->curr_time() >= r->sg.next_packet_start ||
         !r->sg.packet_finished)) {

        //
        // Flit generation.
        //

        int dest = -1;
        if (r->traffic_desc.type == TRF_UNIFORM_RANDOM) {
            dest = r->rand_gen.uni_dist(r->rand_gen.def);
            debugf(r, "Uniform random: dest=%ld\n", dest);
        } else if (r->traffic_desc.type == TRF_DESIGNATED) {
            dest = r->traffic_desc.dests[r->id.value];
        } else {
            assert(false);
        }

        PacketId packet_id{r->id.value, r->sg.packet_counter};
        Flit *flit = new Flit{FLIT_BODY, 0, r->id.value, dest,
                        packet_id, r->sg.flitnum};

        if (r->sg.packet_finished) {
            // Head flit
            //
            if (r->eventq->curr_time() != r->sg.next_packet_start) {
                debugf(r,
                       "WARN: Head flit not generated at the scheduled "
                       "time=%ld!\n",
                       r->sg.next_packet_start);
            }

            flit->type = FLIT_HEAD;
            flit->route_info.path = source_route_compute(
                r, r->top_desc, flit->route_info.src, flit->route_info.dst);
            assert(flit->route_info.path.size() > 0);
            r->sg.flitnum++;

            // Set the time the next packet is generated.
            r->sg.next_packet_start = r->eventq->curr_time() + r->packet_len + 0;
            schedule(r->eventq, r->sg.next_packet_start,
                     tick_event_from_id(r->id));

            // Record packet generation time.
            PacketTimestamp ts{.gen = r->eventq->curr_time(), .arr = -1};
            auto result = r->stat->packet_ledger.insert({flit->packet_id, ts});
            assert(result.second);

            debugf(r, "Source route computation: %d -> %d : {",
                   flit->route_info.src, flit->route_info.dst);
            for (size_t i = 0; i < flit->route_info.path.size(); i++) {
                printf("%d,", flit->route_info.path[i]);
            }
            printf("}\n");

            r->sg.packet_finished = false;
        } else if (r->sg.flitnum == r->packet_len - 1) {
            // Tail flit
            flit->type = FLIT_TAIL;
            r->sg.flitnum = 0;
            r->sg.packet_finished = true;
            r->sg.packet_counter++;
        } else {
            // Body flit
            r->sg.flitnum++;
        }

        if (!r->sg.packet_finished) {
            r->reschedule_next_tick = true;
        }

        queue_put(r->source_queue, flit);

        char s[IDSTRLEN];
        debugf(r, "Flit generated: %s\n", flit_str(flit, s));
        debugf(r, "Source queue len=%ld\n", queue_len(r->source_queue));
    } else if (queue_full(r->source_queue)) {
        debugf(r, "WARN: source queue full!\n");
    }

    // After exiting the source queue.
    if (!queue_empty(r->source_queue)) {
        Flit *ready_flit = queue_front(r->source_queue);

        int ovc_num = r->src_last_grant_output;
        if (ready_flit->type == FLIT_HEAD) {
            // Deadlock avoidance with datelines: always start at the VCs with
            // class 0.
            int ovc_class = 0; /* always */

            // Round-robin VC arbitration
            int vc_per_class = (r->vc_count / r->vc_class_count);
            int ovc_in_class = (r->src_last_grant_output + 1) % vc_per_class;
            for (int i = 0; i < r->vc_class_count; i++) {
                int ovc_num = ovc_class * vc_per_class + ovc_in_class;
                OutputUnit::VC &ovc = r->output_units[TERMINAL_PORT].vcs[ovc_num];
                // Select the first one that has credits.
                if (ovc.credit_count > 0) {
                    r->src_last_grant_output = ovc_num;
                    break;
                }
                ovc_in_class = (ovc_in_class + 1) % vc_per_class;
            }
        }

        OutputUnit::VC &ovc = r->output_units[TERMINAL_PORT].vcs[ovc_num];
        if (ovc.credit_count > 0) {
            queue_pop(r->source_queue);
            // Make sure to mark the VC number in the flit.
            ready_flit->vc_num = ovc_num;
            Channel *och = r->output_channels[TERMINAL_PORT];
            channel_put(och, ready_flit);

            debugf(r, "Source credit decrement, credit=%d->%d\n",
                   ovc.credit_count, ovc.credit_count - 1);
            ovc.credit_count--;
            assert(ovc.credit_count >= 0);

            r->flit_depart_count++;

            char s[IDSTRLEN], s2[IDSTRLEN];
            auto dst_pair = och->conn.dst;
            debugf(r, "Flit sent via VC%d: %s, to {%s, %d}\n", ovc_num,
                   flit_str(ready_flit, s), id_str(dst_pair.id, s2),
                   dst_pair.port);

            // Infinitely generate flits.
            // TODO: Set and control generation rate.
            r->reschedule_next_tick = 1;
        } else {
            debugf(r, "Credit stall!\n");
        }
    }
}

void destination_consume(Router *r)
{
    // Round-robin input VC selection.  Destination node should never block, so
    // keep searching for a non-empty input VC in the single cycle.
    InputUnit::VC *ivc = NULL;
    char s[IDSTRLEN], s2[IDSTRLEN];

    bool has_nonempty_ivc = false;
    int ivc_num = (r->dst_last_grant_input + 1) % r->vc_count;
    for (int i = 0; i < r->vc_count; i++) {
        ivc = &r->input_units[TERMINAL_PORT].vcs[ivc_num];
        if (!queue_empty(ivc->buf)) {
            has_nonempty_ivc = true;
            r->dst_last_grant_input = ivc_num;
            break;
        }
        ivc_num = (ivc_num + 1) % r->vc_count;
    }
    if (!has_nonempty_ivc) {
        // Ideally, the destination node should have never even been scheduled
        // in this case.
        return;
    }

    assert(!queue_empty(ivc->buf));
    Flit *flit = queue_front(ivc->buf);

    if (flit->type == FLIT_HEAD) {
        // First, check if this flit is correctly destined to this node.
        assert(flit->route_info.dst == r->id.value);

        // Record packet arrival time.
        // debugf(r, "Finding packet ID=%ld,%ld\n", flit->packet_id.src,
        // flit->packet_id.id);
        auto f = r->stat->packet_ledger.find(flit->packet_id);
        if (f == r->stat->packet_ledger.end()) {
            printf("src=%ld, id=%ld not found\n", flit->packet_id.src,
                   flit->packet_id.id);
        }
        assert(f != r->stat->packet_ledger.end() &&
               "Packet not recorded upon generation!");
        f->second.arr = r->eventq->curr_time();
        long arr = f->second.arr;
        long gen = f->second.gen;
        long latency = arr - gen;
        // debugf(r, "Deleting packet ID=%ld,%ld\n",
        // flit->packet_id.src, flit->packet_id.id);
        r->stat->packet_ledger.erase(flit->packet_id);

        r->stat->latency_sum += latency;
        r->stat->packet_num++;

        debugf(r,
               "Packet arrived: %s, latency=%ld (arr=%ld, gen=%ld). "
               "mapsize=%ld\n",
               flit_str(flit, s), latency, arr, gen,
               r->stat->packet_ledger.size());
    }

    debugf(r, "Destination buf size=%zd\n", queue_len(ivc->buf));
    debugf(r, "Flit arrived via VC%d: %s\n", ivc_num, flit_str(flit, s));

    r->flit_arrive_count++;
    queue_pop(ivc->buf);
    assert(queue_empty(ivc->buf));

    Channel *ich = r->input_channels[TERMINAL_PORT];
    std::vector<long> vc_nums{ivc_num};

    // FIXME?
    Credit *credit = new Credit{vc_nums};
    channel_put_credit(ich, credit);
    RouterPortPair src_pair = ich->conn.src;
    RouterPortPair dst_pair = ich->conn.dst;
    debugf(r, "Credit sent via VC%d from {%s, %d} to {%s, %d}\n", ivc_num,
           id_str(dst_pair.id, s), dst_pair.port,
           id_str(src_pair.id, s2), src_pair.port);

    // Self-tick autonomously unless all input ports are empty.
    r->reschedule_next_tick = true;

    delete flit;
}

void fetch_flit(Router *r)
{
    for (int iport = 0; iport < r->radix; iport++) {
        Channel *ich = r->input_channels[iport];
        Flit *flit = channel_get(ich);
        if (!flit) {
            continue;
        }

        InputUnit::VC &ivc = r->input_units[iport].vcs[flit->vc_num];

        char s[IDSTRLEN];
        debugf(r, "Fetched flit %s via VC%d, buf[%d][%d].size()=%zd\n",
               flit_str(flit, s), flit->vc_num, iport, flit->vc_num,
               queue_len(ivc.buf));

        // If the buffer was empty, this is the only place to kickstart the
        // pipeline.
        if (queue_empty(ivc.buf)) {
            // debugf(r, "fetch_flit: buf was empty\n");
            // If the input unit state was also idle (empty != idle!), set
            // the stage to RC.
            if (ivc.next_global == STATE_IDLE) {
                // Idle -> RC transition
                ivc.next_global = STATE_ROUTING;
                ivc.stage = PIPELINE_RC;
            }

            r->reschedule_next_tick = true;
        }

        assert(!queue_full(ivc.buf));
        queue_put(ivc.buf, flit);

        assert(queue_len(ivc.buf) <= r->input_buf_size &&
               "Input buffer overflow!");
    }
}

void fetch_credit(Router *r)
{
    for (int oport = 0; oport < r->radix; oport++) {
        Channel *och = r->output_channels[oport];
        Credit *credit = channel_get_credit(och);
        if (credit) {
            debugf(r, "Fetched credit, oport=%d\n", oport);
            for (auto vc_num : credit->vc_nums) {
                OutputUnit::VC &ovc = r->output_units[oport].vcs[vc_num];
                // In any time, there should be at most 1 credit in the buffer.
                assert(queue_empty(ovc.buf_credit));
                queue_put(ovc.buf_credit, credit);
                r->reschedule_next_tick = true;
            }
        }
    }
}

void credit_update(Router *r)
{
    for (int oport = 0; oport < r->radix; oport++) {
        for (int ovc_num = 0; ovc_num < r->vc_count; ovc_num++) {
            OutputUnit::VC &ovc = r->output_units[oport].vcs[ovc_num];

            if (!queue_empty(ovc.buf_credit)) {
                debugf(r, "Credit update! credit=%d->%d (oport=%d)\n",
                       ovc.credit_count, ovc.credit_count + 1, oport);
                assert(ovc.input_port != -1);
                assert(ovc.input_vc != -1);

                // Upon credit update, the input and output unit receiving this
                // credit may or may not be in the CreditWait state.  If they
                // are, make sure to switch them back to the active state so
                // that they can proceed in the SA stage.
                //
                // This can otherwise be implemented in the SA stage itself,
                // switching the stage to Active and simultaneously commencing
                // to the switch allocation.  However, this implementation seems
                // to defeat the purpose of the CreditWait stage. This
                // implementation is what I think of as a more natural one.
                InputUnit::VC &ivc =
                    r->input_units[ovc.input_port].vcs[ovc.input_vc];
                if (ovc.credit_count == 0) {
                    if (ovc.next_global == STATE_CREDWAIT) {
                        assert(ivc.next_global == STATE_CREDWAIT);
                        ivc.next_global = STATE_ACTIVE;
                        ovc.next_global = STATE_ACTIVE;
                    }
                    r->reschedule_next_tick = true;
                    // debugf(r, "credit update with kickstart! (iport=%d)\n",
                    //         ovc.input_port);
                } else {
                    // debugf(r, "credit update, but no kickstart
                    // (credit=%d)\n",
                    //         ovc.credit_count);
                }

                ovc.credit_count++;
                queue_pop(ovc.buf_credit);
                assert(queue_empty(ovc.buf_credit));
            } else {
                // dbg() << "No credit update, oport=" << oport << std::endl;
            }
        }
    }
}

void route_compute(Router *r)
{
    for (int iport = 0; iport < r->radix; iport++) {
        for (int ivc_num = 0; ivc_num < r->vc_count; ivc_num++) {
            InputUnit::VC &ivc = r->input_units[iport].vcs[ivc_num];

            if (ivc.global == STATE_ROUTING) {
                assert(!queue_empty(ivc.buf));
                Flit *flit = queue_front(ivc.buf);

                assert(flit->type == FLIT_HEAD);
                assert(flit->route_info.idx < flit->route_info.path.size());
                ivc.route_port = flit->route_info.path[flit->route_info.idx];
                // ivc.output_vc will be set in the VA stage.

                char s[IDSTRLEN];
                debugf(r, "RC success for %s (idx=%zu, oport=%d)\n",
                       flit_str(flit, s), flit->route_info.idx, ivc.route_port);

                flit->route_info.idx++;

                // RC -> VA transition
                ivc.next_global = STATE_VCWAIT;
                ivc.stage = PIPELINE_VA;
                r->reschedule_next_tick = true;
            }
        }
    }
}

#if 0

// This function expects the given output VC to be in the Idle state.
int vc_arbit_round_robin(Router *r, int out_port)
{
    // Debug: print contenders
    {
        std::vector<int> v;
        for (int i = 0; i < r->radix; i++) {
            InputUnit *iu = &r->input_units[i];
            InputUnit::VC *ivc = &iu->vcs[0 /*FIXME*/];
            if (ivc->global == STATE_VCWAIT && ivc->route_port == out_port)
                v.push_back(i);
        }
        if (!v.empty()) {
            debugf(r, "VA: competing for oport %d from iports {", out_port);
            for (size_t i = 0; i < v.size(); i++)
                printf("%d,", v[i]);
            printf("}\n");
        }
    }

    int iport = (r->va_last_grant_output[out_port] + 1) % r->radix;
    for (int i = 0; i < r->radix; i++) {
        InputUnit *iu = &r->input_units[iport];
        InputUnit::VC *ivc = &iu->vcs[0 /*FIXME*/];
        if (ivc->global == STATE_VCWAIT && ivc->route_port == out_port) {
            // XXX: is VA stage and VCWait state the same?
            assert(ivc->stage == PIPELINE_VA);
            r->va_last_grant_output[out_port] = iport;
            return iport;
        }
        iport = (iport + 1) % r->radix;
    }
    // Indicates that there was no request for this VC.
    return -1;
}

// This function expects the given output VC to be in the Active state.
int sa_arbit_round_robin(Router *r, int out_port)
{
    int iport = (r->sa_last_grant_output[out_port] + 1) % r->radix;
    for (int i = 0; i < r->radix; i++) {
        InputUnit *iu = &r->input_units[iport];
        InputUnit::VC *ivc = &iu->vcs[0 /*FIXME*/];
        // We should check for queue non-emptiness, as it is possible for active
        // input units to have no flits in them because of contention in the
        // upstream router.
        if (ivc->stage == PIPELINE_SA && ivc->route_port == out_port &&
            ivc->global == STATE_ACTIVE && !queue_empty(ivc->buf)) {
            // debugf(r, "SA: granted oport %d to iport %d\n", out_port, iport);
            r->sa_last_grant_output[out_port] = iport;
            return iport;
        } else if (ivc->stage == PIPELINE_SA && ivc->route_port == out_port &&
                   ivc->global == STATE_CREDWAIT) {
            debugf(r, "Credit stall! port=%d\n", ivc->route_port);
        }
        iport = (iport + 1) % r->radix;
    }
    // Indicates that there was no request for this VC.
    return -1;
}

#endif

static size_t alloc_vector_pos(size_t grant_size, size_t input_vc,
                                 size_t output_vc)
{
    return (input_vc * grant_size) + output_vc;
}

// Returns the index of the granted input.
// Accepts a large concatenation of all (xVC) request and grant vectors, and
// modifies the grant vectors in-place.
//
// 'req_size': number of requests/grants waiting for allocation.
// 'grant_size': number of resources available for grant.
// 'which': for which req (if is_input_stage == 1) or grant (if is_input_stage
//          == 0) should I arbitrate at this iteration?
// 'is_input_stage': true of this is input arbitration, false if output
// arbitration.
//
// Returns the VC (global index) whose request won the grant.
size_t round_robin_arbitration(size_t req_size, size_t grant_size, size_t which, bool is_input_stage,
                               size_t last_grant,
                               const std::vector<bool> &request_vectors,
                               std::vector<bool> &grant_vectors)
{
    // Clear the grant vector first.
    for (size_t i = 0; i < req_size; i++) {
        if (is_input_stage) {
            grant_vectors[alloc_vector_pos(grant_size, which, i)] = false;
        } else {
            grant_vectors[alloc_vector_pos(grant_size, i, which)] = false;
        }
    }

    size_t candidate = (last_grant + 1) % req_size;
    for (size_t i = 0; i < req_size; i++) {
        size_t cand_pos;
        if (is_input_stage) {
            cand_pos = alloc_vector_pos(grant_size, which, candidate);
        } else {
            cand_pos = alloc_vector_pos(grant_size, candidate, which);
        }

        if (request_vectors[cand_pos]) {
            grant_vectors[cand_pos] = true;
            return cand_pos;
        }

        candidate = (candidate + 1) % req_size;
    }

    // Indicates that there was no request.
    return -1;
}

// Virtual channel allocation stage.
// Performs a (# of total input VCs) X (# of total output VCs) allocation.
void vc_alloc(Router *r)
{
    //
    // Separable (input-first) allocator.
    //

    // FIXME: PERFORMANCE!

    size_t total_vc = r->radix * r->vc_count;
    size_t vector_size = total_vc * total_vc;
    // Request vectors for each input VC. Has 1 request bit for each output VC.
    std::vector<bool> request_vectors(vector_size, false);
    // Input arbitration result vector, i.e. the 'x' vector in Figure 19.4.
    std::vector<bool> x_vectors(vector_size, false);
    // Grant vectors.
    std::vector<bool> grant_vectors(vector_size, false);

    // Step 0: Prepare request vectors.
    for (int iport = 0; iport < r->radix; iport++) {
        for (int ivc_num = 0; ivc_num < r->vc_count; ivc_num++) {
            InputUnit::VC &ivc = r->input_units[iport].vcs[ivc_num];
            if (ivc.global == STATE_VCWAIT) {
                assert(ivc.route_port >= 0);
                size_t global_ivc = iport * r->vc_count + ivc_num;
                size_t global_ovc_base = ivc.route_port * r->vc_count;
                assert(global_ivc < total_vc);
                assert(global_ovc_base < total_vc);
                // Assert request for all output VCs of the routed oport.

                //
                // Deadlock avoidance: Datelines.
                //

                // Dateline is between the router 3 and 0.
                //
                // If going to the same direction, only allocate VCs with the
                // same number as IVC.  Whenever crossing the dateline,
                // allocate VC with a higher number.
                int vc_per_class = r->vc_count / r->vc_class_count;
                int in_direction = (iport - 1) / 2;
                int out_direction = (ivc.route_port - 1) / 2;
                int ivc_class = ivc_num / vc_per_class;
                int ovc_class =
                    (iport != TERMINAL_PORT && in_direction == out_direction)
                        ? ivc_class
                        : 0;
                int id_in_ring = torus_id_xyz_get(r->id.value, r->top_desc.k, out_direction);
                if ((id_in_ring == (r->top_desc.k - 1) &&
                     ivc.route_port == get_output_port(out_direction, 1)) ||
                    (id_in_ring == 0 &&
                     ivc.route_port == get_output_port(out_direction, 0))) {
                    // If going out to the same direction as coming in, check
                    // that IVC was being maintained as 0.
                    if (iport != TERMINAL_PORT &&
                        in_direction == out_direction) {
                        assert(ivc_class == 0);
                    }
                    ovc_class = 1;
                    debugf(r, "VA: crossing dateline.\n");
                }

                for (int i = 0; i < vc_per_class; i++) {
                    int ovc_num = ovc_class * vc_per_class + i;
                    request_vectors[alloc_vector_pos(
                        total_vc, global_ivc,
                        global_ovc_base + ovc_num)] = true;
                    debugf(r,
                           "VA: request from (iport=%d,VC=%d) -> "
                           "(oport=%d,VC=%d)\n",
                           iport, ivc_num, ivc.route_port, ovc_num);
                }

                // For non-torus topologies:
                // for (int i = 0; i < r->vc_count; i++) {
                //     request_vectors[alloc_vector_pos(total_vc, global_ivc,
                //                                      global_ovc_base + i)] =
                //     true;
                // }
            }
        }
    }

    // Step 1: Input arbitration from request vectors to x-vectors.
    for (size_t global_ivc = 0; global_ivc < total_vc; global_ivc++) {
        size_t winner = round_robin_arbitration(
            total_vc, total_vc, global_ivc, true,
            r->va_last_grant_input[global_ivc], request_vectors, x_vectors);
        if (winner != static_cast<size_t>(-1)) {
            r->va_last_grant_input[global_ivc] = (winner % total_vc);
        }
    }

    // Step 2: Output arbitration from x-vectors to grant vectors.
    for (size_t global_ovc = 0; global_ovc < total_vc; global_ovc++) {
        int oport = global_ovc / r->vc_count;
        int ovc_num = global_ovc % r->vc_count;
        OutputUnit::VC &ovc = r->output_units[oport].vcs[ovc_num];

        // Only do arbitration for available output VCs.
        if (ovc.global == STATE_IDLE) {
            size_t winner = round_robin_arbitration(
                total_vc, total_vc, global_ovc, false,
                r->va_last_grant_output[global_ovc], x_vectors, grant_vectors);
            if (winner != static_cast<size_t>(-1)) {
                r->va_last_grant_output[global_ovc] = (winner / total_vc);
            }
        }
    }

    // Step 3: Update states for the granted VAs.
    int num_grant = 0;
    for (size_t i = 0; i < vector_size; i++) {
        if (grant_vectors[i]) {
            size_t global_ivc = i / total_vc;
            size_t global_ovc = i % total_vc;
            int iport = global_ivc / r->vc_count;
            int ivc_num = global_ivc % r->vc_count;
            int oport = global_ovc / r->vc_count;
            int ovc_num = global_ovc % r->vc_count;

            InputUnit::VC &ivc = r->input_units[iport].vcs[ivc_num];
            OutputUnit::VC &ovc = r->output_units[oport].vcs[ovc_num];

            assert(ivc.global == STATE_VCWAIT);
            assert(ovc.global == STATE_IDLE);
            assert(ivc.route_port == oport);

            char s[IDSTRLEN];
            debugf(r, "VA: success for %s from (iport=%d,VC=%d) to (oport=%d,VC=%d)\n",
                   flit_str(queue_front(ivc.buf), s), iport, ivc_num, oport, ovc_num);

            // We now have the VC, but we cannot proceed to the SA stage
            // if there is no credit.
            if (ovc.credit_count == 0) {
                debugf(r, "VA: no credit, switching to CreditWait\n");
                ivc.next_global = STATE_CREDWAIT;
                ovc.next_global = STATE_CREDWAIT;
            } else {
                ivc.next_global = STATE_ACTIVE;
                ovc.next_global = STATE_ACTIVE;
            }

            // Record the VA result into the input/output units.
            ivc.output_vc = ovc_num;
            ovc.input_port = iport;
            ovc.input_vc = ivc_num;

            ivc.stage = PIPELINE_SA;
            r->reschedule_next_tick = true;

            num_grant++;
        }
    }

    // debugf(r, "VA: granted to %d input VCs.\n", num_grant);
}

// Switch allocation.
// Performs a (# of total input VCs) X (# radix) allocation.
// This is because the switch has no output speedup.
void switch_alloc(Router *r)
{
    //
    // Separable (input-first) allocator.
    //

    // FIXME: PERFORMANCE!

    size_t total_vc = r->radix * r->vc_count;
    size_t vector_size = total_vc * r->radix;
    // Request vectors for each input VC. Has 1 request bit for each output VC.
    std::vector<bool> request_vectors(vector_size, false);
    // Input arbitration result vector, i.e. the 'x' vector in Figure 19.4.
    std::vector<bool> x_vectors(vector_size, false);
    // Grant vectors.
    std::vector<bool> grant_vectors(vector_size, false);

    // Step 0: Prepare request vectors.
    for (int iport = 0; iport < r->radix; iport++) {
        for (int ivc_num = 0; ivc_num < r->vc_count; ivc_num++) {
            InputUnit::VC &ivc = r->input_units[iport].vcs[ivc_num];
            if (ivc.stage == PIPELINE_SA && ivc.global == STATE_ACTIVE &&
                !queue_empty(ivc.buf)) {
                assert(ivc.route_port >= 0);
                size_t global_ivc = iport * r->vc_count + ivc_num;
                size_t oport = ivc.route_port;
                assert(global_ivc < total_vc);
                // Assert request for the routed oport.
                // NOTE: No output speedup.
                request_vectors[alloc_vector_pos(r->radix, global_ivc, oport)] =
                    true;
            }
            // else if (ivc.stage == PIPELINE_SA &&
            //            ivc.route_port == out_port &&
            //            ivc.global == STATE_CREDWAIT) {
            //     debugf(r, "Credit stall! port=%d\n", ivc->route_port);
            // }
        }
    }

    // Step 1: Input arbitration from request vectors to x-vectors.
    for (size_t global_ivc = 0; global_ivc < total_vc; global_ivc++) {
        size_t winner = round_robin_arbitration(
            total_vc, r->radix, global_ivc, true,
            r->sa_last_grant_input[global_ivc], request_vectors, x_vectors);
        if (winner != static_cast<size_t>(-1)) {
            r->sa_last_grant_input[global_ivc] = (winner % r->radix);
        }
    }

    // Step 2: Output arbitration from x-vectors to grant vectors.
    for (int oport = 0; oport < r->radix; oport++) {
        // Unless all VCs of this oport is non-active, attempt to allocate on
        // this port.
        bool oport_has_active_vc = false;
        for (int ovc_num = 0; ovc_num < r->vc_count; ovc_num++) {
            OutputUnit::VC &ovc = r->output_units[oport].vcs[ovc_num];
            if (ovc.global == STATE_ACTIVE) {
                oport_has_active_vc = true;
            }
        }

        if (oport_has_active_vc) {
            // First attempt the arbitration. Then, if the selected OVC is
            // unfortunately the blocked one, disregard it.

            size_t winner = round_robin_arbitration(
                total_vc, r->radix, oport, false,
                r->sa_last_grant_output[oport], x_vectors, grant_vectors);
            if (winner != static_cast<size_t>(-1)) {
                // Now check if the selected OVC is fortunate.
                assert(winner < vector_size);
                size_t global_ivc = winner / r->radix;
                int iport = global_ivc / r->vc_count;
                int ivc_num = global_ivc % r->vc_count;

                InputUnit::VC &ivc = r->input_units[iport].vcs[ivc_num];
                assert(ivc.global == STATE_ACTIVE);
                assert(ivc.output_vc >= 0);
                OutputUnit::VC &ovc = r->output_units[oport].vcs[ivc.output_vc];

                // If unfortunate, the 'speculative' grant turned out to be
                // a miss. Turn off the grant bit back to false.
                if (ovc.global != STATE_ACTIVE) {
                    grant_vectors[winner] = false;
                    debugf(r, "SA: input arbitration picked a block OVC\n");
                } else {
                    // FIXME: Should this be outside of this else?
                    r->sa_last_grant_output[oport] = (winner / r->radix);
                }
            }
        }
    }

    // Step 3: Update states for the granted SAs.
    int num_grant = 0;
    for (size_t i = 0; i < vector_size; i++) {
        if (grant_vectors[i]) {
            size_t global_ivc = i / r->radix;
            int oport = i % r->radix;
            int iport = global_ivc / r->vc_count;
            int ivc_num = global_ivc % r->vc_count;

            // SA success!
            InputUnit::VC &ivc = r->input_units[iport].vcs[ivc_num];
            // ovc_num should be read from ivc.
            OutputUnit::VC &ovc = r->output_units[oport].vcs[ivc.output_vc];

            assert(ivc.global == STATE_ACTIVE);
            assert(ovc.global == STATE_ACTIVE);
            // Because sa_arbit_round_robin only selects input units that
            // has flits in them, the input queue cannot be empty.
            assert(!queue_empty(ivc.buf));

            char s[IDSTRLEN];
            debugf(r,
                   "SA: success for %s from (iport=%d,VC=%d) to (oport = % d, "
                   "VC = % d)\n",
                   flit_str(queue_front(ivc.buf), s), iport, ivc_num, oport,
                   ivc.output_vc);

            // The flit leaves the input buffer here.
            Flit *flit = queue_front(ivc.buf);
            queue_pop(ivc.buf);
            assert(!ivc.st_ready);
            ivc.st_ready = flit;

            // Credit decrement.
            debugf(r, "Credit decrement, credit=%d->%d (oport=%d)\n",
                   ovc.credit_count, ovc.credit_count - 1, oport);
            assert(ovc.credit_count > 0);
            ovc.credit_count--;

            // SA -> ?? transition
            //
            // Set the next stage according to the flit type and credit
            // count.
            //
            // Note that switching state to CreditWait does NOT prevent the
            // subsequent ST to happen. The flit that has succeeded SA on
            // this cycle is transferred to ivc.st_ready, and that is the
            // only thing that is visible to the ST stage.
            if (flit->type == FLIT_TAIL) {
                ovc.next_global = STATE_IDLE;
                if (queue_empty(ivc.buf)) {
                    ivc.next_global = STATE_IDLE;
                    ivc.stage = PIPELINE_IDLE;
                    // debugf(this, "SA: next state is Idle\n");
                } else {
                    ivc.next_global = STATE_ROUTING;
                    ivc.stage = PIPELINE_RC;
                    // debugf(this, "SA: next state is Routing\n");
                }
                r->reschedule_next_tick = true;
            } else if (ovc.credit_count == 0) {
                // debugf(r, "SA: switching to CW\n");
                ivc.next_global = STATE_CREDWAIT;
                ovc.next_global = STATE_CREDWAIT;
                // debugf(this, "SA: next state is CreditWait\n");
            } else {
                ivc.next_global = STATE_ACTIVE;
                ivc.stage = PIPELINE_SA;
                // debugf(this, "SA: next state is Active\n");
                r->reschedule_next_tick = true;
            }
            assert(ovc.credit_count >= 0);

            num_grant++;
        }
    }
}

void switch_traverse(Router *r)
{
    char s[IDSTRLEN], s2[IDSTRLEN], s3[IDSTRLEN];

    for (int iport = 0; iport < r->radix; iport++) {
        std::vector<long> vc_nums;
        for (int ivc_num = 0; ivc_num < r->vc_count; ivc_num++) {
            InputUnit::VC &ivc = r->input_units[iport].vcs[ivc_num];

            if (ivc.st_ready) {
                Flit *flit = ivc.st_ready;
                ivc.st_ready = NULL;

                // Caution: be sure to update the VC field in the flit.
                assert(flit->vc_num == ivc_num);
                flit->vc_num = ivc.output_vc;

                // No output speedup: there is no need for an output buffer
                // (Ch17.3).  Flits that exit the switch are directly placed on
                // the channel.
                Channel *och = r->output_channels[ivc.route_port];
                channel_put(och, flit);
                RouterPortPair src_pair = och->conn.src;
                RouterPortPair dst_pair = och->conn.dst;

                debugf(
                    r,
                    "Switch traverse: %s sent via VC%d from {%s, %d} to {%s, "
                    "%d}\n",
                    flit_str(flit, s), ivc.output_vc, id_str(src_pair.id, s2),
                    src_pair.port, id_str(dst_pair.id, s3), dst_pair.port);

                // With output speedup:
                // auto &ou = output_units[ivc.route_port];
                // ou->buf.push_back(flit);

                vc_nums.push_back(ivc_num);
            }
        }

        if (!vc_nums.empty()) {
            // CT stage: return credit to the upstream node.
            // Caution: do this once per input port.
            Channel *ich = r->input_channels[iport];
            // FIXME?
            Credit *credit = new Credit{vc_nums};
            channel_put_credit(ich, credit);
            RouterPortPair credit_src_pair = ich->conn.src;
            RouterPortPair credit_dst_pair = ich->conn.dst;
            for (auto vc_num : vc_nums) {
                debugf(r, "Credit sent via VC%d from {%s, %d} to {%s, %d}\n",
                       vc_num, id_str(credit_dst_pair.id, s),
                       credit_dst_pair.port, id_str(credit_src_pair.id, s2),
                       credit_src_pair.port);
            }
        }
    }
}

void update_states(Router *r)
{
    int changed = 0;
    for (int port = 0; port < r->radix; port++) {
        for (int vc_num = 0; vc_num < r->vc_count; vc_num++) {
            InputUnit::VC &ivc = r->input_units[port].vcs[vc_num];
            OutputUnit::VC &ovc = r->output_units[port].vcs[vc_num];
            if (ivc.global != ivc.next_global) {
                ivc.global = ivc.next_global;
                changed = 1;
            }
            if (ovc.global != ovc.next_global) {
                assert(!(ovc.next_global == STATE_CREDWAIT &&
                         ovc.credit_count > 0));
                ovc.global = ovc.next_global;
                changed = 1;
            }
        }
    }
    // Reschedule whenever there is one or more state change.
    if (changed) r->reschedule_next_tick = true;
}

void router_print_state(Router *r)
{
    char s[IDSTRLEN];
    printf("[%s]\n", id_str(r->id, s));

    for (int i = 0; i < r->radix; i++) {
        for (int ivc_num = 0; ivc_num < r->vc_count; ivc_num++) {
            InputUnit::VC &ivc = r->input_units[i].vcs[ivc_num];
            printf(" Input[%d,VC%d]: [%s] R=%2d, OVC=%2d {", i, ivc_num,
                    globalstate_str(ivc.global, s), ivc.route_port,
                    ivc.output_vc);
            for (long i = queue_fronti(ivc.buf); i != queue_backi(ivc.buf);
                 i = (i + 1) % queue_cap(ivc.buf)) {
                Flit *flit = ivc.buf[i];
                printf("%s,", flit_str(flit, s));
            }
            printf("} ST:%s\n", flit_str(ivc.st_ready, s));
        }
    }

    for (int i = 0; i < r->radix; i++) {
        for (int ovc_num = 0; ovc_num < r->vc_count; ovc_num++) {
            OutputUnit::VC &ovc = r->output_units[i].vcs[ovc_num];
            printf("Output[%d,VC%d]: [%s] I=%2d, IVC=%2d, C=%2d\n", i, ovc_num,
                    globalstate_str(ovc.global, s), ovc.input_port,
                    ovc.input_vc, ovc.credit_count);
        }
    }

    for (int i = 0; i < r->radix; i++) {
        Channel *ch = r->input_channels[i];
        printf("InChannel[%d]: {", i);
        for (long i = queue_fronti(ch->buf); i != queue_backi(ch->buf);
             i = (i + 1) % queue_cap(ch->buf)) {
            TimedFlit tf = ch->buf[i];
            printf("%ld:%s,", tf.time, flit_str(tf.flit, s));
        }
        printf("}\n");
    }
}
