#include "router.h"
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
    : gen(), rd(), uni_dist(0, terminal_count - 1)
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

Channel channel_create(EventQueue *eq, long dl, const Connection conn)
{
    Channel ch;
    ch.conn = conn;
    ch.eventq = eq;
    ch.delay = dl;
    ch.buf = NULL;
    ch.buf_credit = NULL;
    queue_init(ch.buf, dl + CHANNEL_SLACK);
    queue_init(ch.buf_credit, CHANNEL_SLACK);
    return ch;
}

void channel_destroy(Channel *ch)
{
    queue_free(ch->buf);
    queue_free(ch->buf_credit);
}

void channel_put(Channel *ch, Flit *flit)
{
    TimedFlit tf = {curr_time(ch->eventq) + ch->delay, flit};
    assert(!queue_full(ch->buf));
    queue_put(ch->buf, tf);
    reschedule(ch->eventq, ch->delay, tick_event_from_id(ch->conn.dst.id));
}

void channel_put_credit(Channel *ch, Credit credit)
{
    TimedCredit tc = {curr_time(ch->eventq) + ch->delay, credit};
    assert(!queue_full(ch->buf_credit));
    queue_put(ch->buf_credit, tc);
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

int channel_get_credit(Channel *ch, Credit *c)
{
    TimedCredit front = queue_front(ch->buf_credit);
    if (!queue_empty(ch->buf_credit) && curr_time(ch->eventq) >= front.time) {
        assert(curr_time(ch->eventq) == front.time && "stale flit!");
        queue_pop(ch->buf_credit);
        *c = front.credit;
        return 1;
    } else {
        return 0;
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

Flit::Flit(enum FlitType t, int src, int dst, PacketId pid, long flitnum)
    : type(t), packet_id(pid), flitnum(flitnum)
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

static InputUnit inputunit_create(int bufsize)
{
    Flit **buf = NULL;
    queue_init(buf, bufsize * 2);
    return (InputUnit){
        .global = STATE_IDLE,
        .next_global = STATE_IDLE,
        .route_port = -1,
        .output_vc = 0,
        .stage = PIPELINE_IDLE,
        .buf = buf,
        .st_ready = NULL,
    };
}

static OutputUnit outputunit_create(int bufsize)
{
    Credit *buf_credit = NULL;
    queue_init(buf_credit, bufsize * 2); // FIXME: unnecessarily big.
    return (OutputUnit){
        .global = STATE_IDLE,
        .next_global = STATE_IDLE,
        .input_port = -1,
        .input_vc = 0,
        .credit_count = bufsize,
        .buf_credit = buf_credit,
    };
}

static void inputunit_destroy(InputUnit *iu)
{
    queue_free(iu->buf);
}

static void outputunit_destroy(OutputUnit *ou)
{
    queue_free(ou->buf_credit);
}

Router::Router(EventQueue *eq, Id id, int radix, Stat *st, TopoDesc td,
               TrafficDesc trd, RandomGenerator &rg, long packet_len,
               Channel **in_chs, Channel **out_chs, long input_buf_size)
    : id(id), radix(radix), eventq(eq), stat(st), top_desc(td),
      traffic_desc(trd), rand_gen(rg), packet_len(packet_len),
      input_buf_size(input_buf_size)
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

    input_units = NULL;
    output_units = NULL;
    for (int port = 0; port < radix; port++) {
        InputUnit iu = inputunit_create(input_buf_size);
        OutputUnit ou = outputunit_create(input_buf_size);
        arrput(input_units, iu);
        arrput(output_units, ou);
    }

    if (is_src(id) || is_dst(id)) {
        assert(arrlen(input_units) == 1);
        assert(arrlen(output_units) == 1);
        // There are no route computation stages for terminal nodes, so set the
        // routed ports for each IU/OU statically here.
        input_units[0].route_port = TERMINAL_PORT;
        output_units[0].input_port = TERMINAL_PORT;
    }
}

Router::~Router()
{
    for (int port = 0; port < radix; port++) {
        inputunit_destroy(&input_units[port]);
        outputunit_destroy(&output_units[port]);
    }
    arrfree(input_units);
    arrfree(output_units);
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
static void source_route_compute_dimension(TopoDesc td, int src_id,
                                                        int dst_id,
                                                        int direction,
                                                        std::vector<int> &path)
{
    int total = td.k;
    int src_id_xyz = torus_id_xyz_get(src_id, td.k, direction);
    int dst_id_xyz = torus_id_xyz_get(dst_id, td.k, direction);
    int cw_dist = (dst_id_xyz - src_id_xyz + total) % total;

    if (cw_dist <= total / 2) {
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
std::vector<int> source_route_compute(TopoDesc td, int src_id, int dst_id)
{
    std::vector<int> path;

    // Dimension-order routing. Order is XYZ.
    int last_src_id = src_id;
    for (int dir = 0; dir < td.r; dir++) {
        int interim_id = torus_align_id(td.k, last_src_id, dst_id, dir);
        // printf("%s: from %d to %d\n", __func__, last_src_id, interim_id);
        source_route_compute_dimension(td, last_src_id, interim_id, dir, path);
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
    OutputUnit *ou = &r->output_units[0];

    // Before entering the source queue.
    if (!queue_full(r->source_queue) &&
        (r->eventq->curr_time() >= r->sg.next_packet_start ||
         !r->sg.packet_finished)) {

        //
        // Flit generation.
        //

        int dest = -1;
        if (r->traffic_desc.type == TRF_UNIFORM_RANDOM) {
            dest = r->rand_gen.uni_dist(r->rand_gen.rd);
            debugf(r, "Uniform random: dest=%ld\n", dest);
        } else if (r->traffic_desc.type == TRF_DESIGNATED) {
            dest = r->traffic_desc.dests[r->id.value];
        } else {
            assert(false);
        }

        PacketId packet_id{r->id.value, r->sg.packet_counter};
        Flit *flit = new Flit{FLIT_BODY, r->id.value, dest,
                        packet_id, r->sg.flitnum};

        if (r->sg.packet_finished) {
            if (r->eventq->curr_time() != r->sg.next_packet_start) {
                debugf(r,
                       "WARN: Head flit not generated at the scheduled "
                       "time=%ld!\n",
                       r->sg.next_packet_start);
            }

            // Head flit
            r->sg.packet_counter++;

            flit->type = FLIT_HEAD;
            flit->route_info.path = source_route_compute(
                r->top_desc, flit->route_info.src, flit->route_info.dst);
            r->sg.flitnum++;

            // Set the time the next packet is generated.
            r->sg.next_packet_start = r->eventq->curr_time() + 13;
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
        } else if (r->sg.flitnum == PACKET_SIZE - 1) {
            // Tail flit
            flit->type = FLIT_TAIL;
            r->sg.flitnum = 0;
            r->sg.packet_finished = true;
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
    if (!queue_empty(r->source_queue) && ou->credit_count > 0) {
        Flit *ready_flit = queue_front(r->source_queue);
        queue_pop(r->source_queue);
        Channel *och = r->output_channels[0];
        channel_put(och, ready_flit);

        debugf(r, "Source credit decrement, credit=%d->%d\n", ou->credit_count,
               ou->credit_count - 1);
        ou->credit_count--;
        assert(ou->credit_count >= 0);

        r->flit_depart_count++;

        char s[IDSTRLEN];
        debugf(r, "Flit sent: %s\n", flit_str(ready_flit, s));

        // Infinitely generate flits.
        // TODO: Set and control generation rate.
        // r->reschedule_next_tick = 1;
    } else {
        debugf(r, "Credit stall!\n");
    }
}

void destination_consume(Router *r)
{
    InputUnit *iu = &r->input_units[0];
    char s[IDSTRLEN];

    if (!queue_empty(iu->buf)) {
        Flit *flit = queue_front(iu->buf);

        if (flit->type == FLIT_HEAD) {
            // Record packet arrival time.
            // debugf(r, "Finding packet ID=%ld,%ld\n", flit->packet_id.src,
            // flit->packet_id.id);
            auto f = r->stat->packet_ledger.find(flit->packet_id);
            if (f == r->stat->packet_ledger.end()) {
                printf("src=%ld, id=%ld not found\n", flit->packet_id.src, flit->packet_id.id);
            }
            assert(f != r->stat->packet_ledger.end() &&
                   "Packet not recorded upon generation!");
            f->second.arr = r->eventq->curr_time();
            long arr = f->second.arr;
            long gen = f->second.gen;
            long latency = arr - gen;
            // debugf(r, "Deleting packet ID=%ld,%ld\n", flit->packet_id.src,
            // flit->packet_id.id);
            r->stat->packet_ledger.erase(flit->packet_id);

            r->stat->latency_sum += latency;
            r->stat->packet_num++;

            debugf(r, "Packet arrived: %s, latency=%ld (arr=%ld, gen=%ld). mapsize=%ld\n",
                   flit_str(flit, s), latency, arr, gen, r->stat->packet_ledger.size());
        }

        debugf(r, "Destination buf size=%zd\n", queue_len(iu->buf));
        debugf(r, "Flit arrived: %s\n", flit_str(flit, s));

        r->flit_arrive_count++;
        queue_pop(iu->buf);
        assert(queue_empty(iu->buf));

        Channel *ich = r->input_channels[0];
        channel_put_credit(ich, (Credit){});

        RouterPortPair src_pair = ich->conn.src;
        debugf(r, "Credit sent to {%s, %d}\n", id_str(src_pair.id, s),
               src_pair.port);

        // Self-tick autonomously unless all input ports are empty.
        r->reschedule_next_tick = true;

        delete flit;
    }
}

void fetch_flit(Router *r)
{
    for (int iport = 0; iport < r->radix; iport++) {
        Channel *ich = r->input_channels[iport];
        InputUnit *iu = &r->input_units[iport];
        Flit *flit = channel_get(ich);
        if (!flit)
            continue;

        char s[IDSTRLEN];
        debugf(r, "Fetched flit %s, buf[%d].size()=%zd\n", flit_str(flit, s),
               iport, queue_len(iu->buf));

        // If the buffer was empty, this is the only place to kickstart the
        // pipeline.
        if (queue_empty(iu->buf)) {
            // debugf(r, "fetch_flit: buf was empty\n");
            // If the input unit state was also idle (empty != idle!), set
            // the stage to RC.
            if (iu->next_global == STATE_IDLE) {
                // Idle -> RC transition
                iu->next_global = STATE_ROUTING;
                iu->stage = PIPELINE_RC;
            }

            r->reschedule_next_tick = true;
        }

        assert(!queue_full(iu->buf));
        queue_put(iu->buf, flit);

        assert(queue_len(iu->buf) <= r->input_buf_size &&
               "Input buffer overflow!");
    }
}

void fetch_credit(Router *r)
{
    for (int oport = 0; oport < r->radix; oport++) {
        Channel *och = r->output_channels[oport];
        Credit c;
        int got = channel_get_credit(och, &c);
        if (got) {
            OutputUnit *ou = &r->output_units[oport];
            debugf(r, "Fetched credit, oport=%d\n", oport);
            // In any time, there should be at most 1 credit in the buffer.
            assert(queue_empty(ou->buf_credit));
            queue_put(ou->buf_credit, c);
            r->reschedule_next_tick = true;
        }
    }
}

void credit_update(Router *r)
{
    for (int oport = 0; oport < r->radix; oport++) {
        OutputUnit *ou = &r->output_units[oport];
        if (!queue_empty(ou->buf_credit)) {
            debugf(r, "Credit update! credit=%d->%d (oport=%d)\n",
                   ou->credit_count, ou->credit_count + 1, oport);
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
            InputUnit *iu = &r->input_units[ou->input_port];
            if (ou->credit_count == 0) {
                if (ou->next_global == STATE_CREDWAIT) {
                    assert(iu->next_global == STATE_CREDWAIT);
                    iu->next_global = STATE_ACTIVE;
                    ou->next_global = STATE_ACTIVE;
                }
                r->reschedule_next_tick = true;
                // debugf(r, "credit update with kickstart! (iport=%d)\n",
                //         ou->input_port);
            } else {
                // debugf(r, "credit update, but no kickstart (credit=%d)\n",
                //         ou->credit_count);
            }

            ou->credit_count++;
            queue_pop(ou->buf_credit);
            assert(queue_empty(ou->buf_credit));
        } else {
            // dbg() << "No credit update, oport=" << oport << std::endl;
        }
    }
}

void route_compute(Router *r)
{
    for (int iport = 0; iport < r->radix; iport++) {
        InputUnit *iu = &r->input_units[iport];

        if (iu->global == STATE_ROUTING) {
            assert(!queue_empty(iu->buf));
            Flit *flit = queue_front(iu->buf);

            assert(flit->type == FLIT_HEAD);
            assert(flit->route_info.idx < flit->route_info.path.size());
            iu->route_port = flit->route_info.path[flit->route_info.idx];

            char s[IDSTRLEN];
            debugf(r, "RC success for %s (idx=%zu, oport=%d)\n",
                   flit_str(flit, s), flit->route_info.idx, iu->route_port);

            flit->route_info.idx++;

            // RC -> VA transition
            iu->next_global = STATE_VCWAIT;
            iu->stage = PIPELINE_VA;
            r->reschedule_next_tick = true;
        }
    }
}

// This function expects the given output VC to be in the Idle state.
int vc_arbit_round_robin(Router *r, int out_port)
{
    // Debug: print contenders
    int *v = NULL;
    for (int i = 0; i < r->radix; i++) {
        InputUnit *iu = &r->input_units[i];
        if (iu->global == STATE_VCWAIT && iu->route_port == out_port)
            arrput(v, i);
    }
    if (arrlen(v)) {
        debugf(r, "VA: competing for oport %d from iports {", out_port);
        for (int i = 0; i < arrlen(v); i++)
            printf("%d,", v[i]);
        printf("}\n");
    }
    arrfree(v);

    int iport = (r->va_last_grant_input + 1) % r->radix;
    for (int i = 0; i < r->radix; i++) {
        InputUnit *iu = &r->input_units[iport];
        if (iu->global == STATE_VCWAIT && iu->route_port == out_port) {
            // XXX: is VA stage and VCWait state the same?
            assert(iu->stage == PIPELINE_VA);
            r->va_last_grant_input = iport;
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
    int iport = (r->sa_last_grant_input + 1) % r->radix;
    for (int i = 0; i < r->radix; i++) {
        InputUnit *iu = &r->input_units[iport];
        // We should check for queue non-emptiness, as it is possible for active
        // input units to have no flits in them because of contention in the
        // upstream router.
        if (iu->stage == PIPELINE_SA && iu->route_port == out_port &&
            iu->global == STATE_ACTIVE && !queue_empty(iu->buf)) {
            // debugf(r, "SA: granted oport %d to iport %d\n", out_port, iport);
            r->sa_last_grant_input = iport;
            return iport;
        } else if (iu->stage == PIPELINE_SA && iu->route_port == out_port &&
                   iu->global == STATE_CREDWAIT) {
            debugf(r, "Credit stall! port=%d\n", iu->route_port);
        }
        iport = (iport + 1) % r->radix;
    }
    // Indicates that there was no request for this VC.
    return -1;
}

void vc_alloc(Router *r)
{
    // dbg() << "VC allocation\n";

    for (int oport = 0; oport < r->radix; oport++) {
        OutputUnit *ou = &r->output_units[oport];

        // Only do arbitration for inactive output VCs.
        if (ou->global == STATE_IDLE) {
            // Arbitration
            int iport = vc_arbit_round_robin(r, oport);

            if (iport == -1) {
                // dbg() << "no pending VC request for oport=" << oport <<
                // std::endl;
            } else {
                InputUnit *iu = &r->input_units[iport];

                char s[IDSTRLEN];
                debugf(r, "VA: success for %s from iport %d to oport %d\n",
                       flit_str(queue_front(iu->buf), s), iport, oport);

                // We now have the VC, but we cannot proceed to the SA stage if
                // there is no credit.
                if (ou->credit_count == 0) {
                    debugf(r, "VA: no credit, switching to CreditWait\n");
                    iu->next_global = STATE_CREDWAIT;
                    ou->next_global = STATE_CREDWAIT;
                } else {
                    iu->next_global = STATE_ACTIVE;
                    ou->next_global = STATE_ACTIVE;
                }

                // Record the input port to the Output unit.
                ou->input_port = iport;

                iu->stage = PIPELINE_SA;
                r->reschedule_next_tick = true;
            }
        }
    }
}

void switch_alloc(Router *r)
{
    for (int oport = 0; oport < r->radix; oport++) {
        OutputUnit *ou = &r->output_units[oport];

        // Only do arbitration for output VCs that has available credits.
        if (ou->global == STATE_ACTIVE) {
            // Arbitration
            int iport = sa_arbit_round_robin(r, oport);

            if (iport == -1) {
                // dbg() << "no pending SA request!\n";
            } else {
                // SA success!
                InputUnit *iu = &r->input_units[iport];
                assert(iu->global == STATE_ACTIVE);
                assert(ou->global == STATE_ACTIVE);
                // Because sa_arbit_round_robin only selects input units that
                // has flits in them, the input queue cannot be empty.
                assert(!queue_empty(iu->buf));

                char s[IDSTRLEN];
                debugf(r, "SA success for %s from iport %d to oport %d\n",
                       flit_str(queue_front(iu->buf), s), iport, oport);

                // The flit leaves the buffer here.
                Flit *flit = queue_front(iu->buf);
                queue_pop(iu->buf);
                assert(!iu->st_ready);
                iu->st_ready = flit;

                // Credit decrement.
                debugf(r, "Credit decrement, credit=%d->%d (oport=%d)\n",
                       ou->credit_count, ou->credit_count - 1, oport);
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
                if (flit->type == FLIT_TAIL) {
                    ou->next_global = STATE_IDLE;
                    if (queue_empty(iu->buf)) {
                        iu->next_global = STATE_IDLE;
                        iu->stage = PIPELINE_IDLE;
                        // debugf(this, "SA: next state is Idle\n");
                    } else {
                        iu->next_global = STATE_ROUTING;
                        iu->stage = PIPELINE_RC;
                        // debugf(this, "SA: next state is Routing\n");
                    }
                    r->reschedule_next_tick = true;
                } else if (ou->credit_count == 0) {
                    // debugf(r, "SA: switching to CW\n");
                    iu->next_global = STATE_CREDWAIT;
                    ou->next_global = STATE_CREDWAIT;
                    // debugf(this, "SA: next state is CreditWait\n");
                } else {
                    iu->next_global = STATE_ACTIVE;
                    iu->stage = PIPELINE_SA;
                    // debugf(this, "SA: next state is Active\n");
                    r->reschedule_next_tick = true;
                }
                assert(ou->credit_count >= 0);
            }
        }
    }
}

void switch_traverse(Router *r)
{
    for (int iport = 0; iport < r->radix; iport++) {
        InputUnit *iu = &r->input_units[iport];

        if (iu->st_ready) {
            Flit *flit = iu->st_ready;
            iu->st_ready = NULL;

            // No output speedup: there is no need for an output buffer
            // (Ch17.3).  Flits that exit the switch are directly placed on the
            // channel.
            Channel *och = r->output_channels[iu->route_port];
            channel_put(och, flit);
            RouterPortPair dst_pair = och->conn.dst;

            char s[IDSTRLEN], s2[IDSTRLEN];
            debugf(r, "Switch traverse: %s sent to {%s, %d}\n",
                   flit_str(flit, s), id_str(dst_pair.id, s2), dst_pair.port);

            // With output speedup:
            // auto &ou = output_units[iu->route_port];
            // ou->buf.push_back(flit);

            // CT stage: return credit to the upstream node.
            Channel *ich = r->input_channels[iport];
            channel_put_credit(ich, (Credit){});
            RouterPortPair src_pair = ich->conn.src;
            debugf(r, "Credit sent to {%s, %d}\n", id_str(src_pair.id, s),
                   src_pair.port);
        }
    }
}

void update_states(Router *r)
{
    int changed = 0;
    for (int port = 0; port < r->radix; port++) {
        InputUnit *iu = &r->input_units[port];
        OutputUnit *ou = &r->output_units[port];
        if (iu->global != iu->next_global) {
            iu->global = iu->next_global;
            changed = 1;
        }
        if (ou->global != ou->next_global) {
            assert(!(ou->next_global == STATE_CREDWAIT && ou->credit_count > 0));
            ou->global = ou->next_global;
            changed = 1;
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
        InputUnit *iu = &r->input_units[i];
        printf(" Input[%d]: [%s] R=%2d {", i, globalstate_str(iu->global, s),
               iu->route_port);
        for (long i = queue_fronti(iu->buf); i != queue_backi(iu->buf);
             i = (i + 1) % queue_cap(iu->buf)) {
            Flit *flit = iu->buf[i];
            printf("%s,", flit_str(flit, s));
        }
        printf("} ST:%s\n", flit_str(iu->st_ready, s));
    }

    for (int i = 0; i < r->radix; i++) {
        OutputUnit *ou = &r->output_units[i];
        printf("Output[%d]: [%s] I=%2d, C=%2d\n", i,
               globalstate_str(ou->global, s), ou->input_port,
               ou->credit_count);
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
