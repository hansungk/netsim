#include "router.h"
#include "queue.h"
#include "stb_ds.h"
#include <stdio.h>
#include <assert.h>

static void dprintf(Router *r, const char *fmt, ...)
{
    char s[IDSTRLEN];
    printf("[@%3ld] [%s] ", r->eventq->curr_time(), id_str(r->id, s));
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

Event tick_event_from_id(Id id)
{
    return Event{id, router_tick};
}

Channel::Channel(EventQueue *eq, long dl, const Connection conn)
        : conn(conn), eventq(eq), delay(dl)
{
    queue_init(buf, 4); // FIXME hardcoded
    queue_init(buf_credit, 4); // FIXME hardcoded
}

void channel_destroy(Channel *ch)
{
    queue_free(ch->buf);
    queue_free(ch->buf_credit);
}

void Channel::put(Flit *flit)
{
    TimedFlit tf = {eventq->curr_time() + delay, flit};
    queue_put(buf, tf);
    eventq->reschedule(delay, tick_event_from_id(conn.dst.id));
}

void Channel::put_credit(const Credit &credit)
{
    TimedCredit tc = {eventq->curr_time() + delay, credit};
    queue_put(buf_credit, tc);
    eventq->reschedule(delay, tick_event_from_id(conn.src.id));
}

Flit *Channel::get()
{
    TimedFlit front = queue_front(buf);
    if (!queue_empty(buf) && eventq->curr_time() >= front.time) {
        assert(eventq->curr_time() == front.time && "stagnant flit!");
        Flit *flit = front.flit;
        queue_pop(buf);
        return flit;
    } else {
        return NULL;
    }
}

bool Channel::get_credit(Credit *c)
{
    TimedCredit front = queue_front(buf_credit);
    if (!queue_empty(buf_credit) && eventq->curr_time() >= front.time) {
        assert(eventq->curr_time() == front.time && "stagnant flit!");
        queue_pop(buf_credit);
        *c = front.credit;
        return true;
    } else {
        return false;
    }
}

void print_conn(const char *name, Connection conn)
{
    printf("%s: %d.%d.%d -> %d.%d.%d\n", name, conn.src.id.type,
           conn.src.id.value, conn.src.port, conn.dst.id.type,
           conn.dst.id.value, conn.dst.port);
}

Topology topology_create(void)
{
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
    if (hmgeti(t->forward_hash, input) >= 0 ||
        hmgeti(t->reverse_hash, output) >= 0)
        // Bad connectivity: source or destination port is already connected
        return false;
    int uniq = hmlen(t->forward_hash);
    Connection conn = (Connection){.src = input, .dst = output, .uniq = uniq};
    hmput(t->forward_hash, input, conn);
    hmput(t->reverse_hash, output, conn);
    assert(hmgeti(t->forward_hash, input) >= 0);
    return true;
}

bool topology_connect_terminals(Topology *t, const int *ids)
{
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
bool topology_connect_ring(Topology *t, const int *ids)
{
    bool res = true;
    for (long i = 0; i < arrlen(ids); i++) {
        int l = ids[i];
        int r = ids[(i + 1) % arrlen(ids)];
        RouterPortPair lport{rtr_id(l), 2};
        RouterPortPair rport{rtr_id(r), 1};

        // Bidirectional channel
        res &= topology_connect(t, lport, rport);
        res &= topology_connect(t, rport, lport);
        if (!res)
            return false;
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

void topology_connect_torus_dimension(Topology *t, int k, int r, int offset)
{
}

Topology topology_torus(int k, int r)
{
    Topology top = topology_create();
    // int *ids = NULL;
    bool res = true;

    // Horizontal
    int stride = k;
    for (int i = 0; i < r; i++) {
        int *ids = NULL;
        for (int j = 0; j < k; j++) {
            arrput(ids, k * i + j);
        }
        res &= topology_connect_ring(&top, ids);
        arrfree(ids);

        stride *= k;
    }
    // Vertical

    for (int i = 0; i < r; i++) {
        int *ids = NULL;
        for (int j = 0; j < k; j++) {
            arrput(ids, k * j + i);
        }
        res &= topology_connect_ring(&top, ids);
        arrfree(ids);
    }

    return top;
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

Flit *flit_create(FlitType t, int src, int dst, long p)
{
    Flit *flit = (Flit *)malloc(sizeof(Flit));
    RouteInfo ri = {src, dst, {}, 0};
    *flit = (Flit){.type = t, .route_info = ri, .payload = p};
    return flit;
}

void flit_destroy(Flit *flit)
{
    if (flit->route_info.path)
        arrfree(flit->route_info.path);
    free(flit);
}

char *flit_str(const Flit *flit, char *s)
{
    // FIXME: Rename IDSTRLEN!
    if (flit)
        snprintf(s, IDSTRLEN, "{%d.p%ld}", flit->route_info.src, flit->payload);
    else
        snprintf(s, IDSTRLEN, "");
    return s;
}

char *globalstate_str(GlobalState state, char *s)
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

static InputUnit input_unit_create(int bufsize)
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

static OutputUnit output_unit_create(int bufsize)
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

static void input_unit_destroy(InputUnit *iu)
{
    queue_free(iu->buf);
}

static void output_unit_destroy(OutputUnit *ou)
{
    queue_free(ou->buf_credit);
}

Router router_create(EventQueue *eq, Alloc *fa, Stat *st, TopoDesc td, Id id,
                     int radix, Channel **in_chs, Channel **out_chs)
{
    Channel **input_channels = NULL;
    Channel **output_channels = NULL;
    for (long i = 0; i < arrlen(in_chs); i++)
        arrput(input_channels, in_chs[i]);
    for (long i = 0; i < arrlen(out_chs); i++)
        arrput(output_channels, out_chs[i]);

    size_t input_buf_size = 100;

    InputUnit *input_units = NULL;
    OutputUnit *output_units = NULL;
    for (int port = 0; port < radix; port++) {
        InputUnit iu = input_unit_create(input_buf_size);
        OutputUnit ou = output_unit_create(input_buf_size);
        arrput(input_units, iu);
        arrput(output_units, ou);
    }

    if (is_src(id) || is_dst(id)) {
        assert(arrlen(input_units) == 1);
        assert(arrlen(output_units) == 1);
        // There are no route computation stages for terminal nodes, so set the
        // routed ports for each IU/OU statically here.
        input_units[0].route_port = 0;
        output_units[0].input_port = 0;
    }

    return (Router){
        .id = id,
        .radix = radix,
        .eventq = eq,
        .flit_allocator = fa,
        .stat = st,
        .top_desc = td,
        .last_tick = -1,
        .input_channels = input_channels,
        .output_channels = output_channels,
        .input_units = input_units,
        .output_units = output_units,
        .input_buf_size = input_buf_size,
    };
}

void router_destroy(Router *r)
{
    for (int port = 0; port < r->radix; port++) {
        input_unit_destroy(&r->input_units[port]);
        output_unit_destroy(&r->output_units[port]);
    }
    arrfree(r->input_units);
    arrfree(r->output_units);
    arrfree(r->input_channels);
    arrfree(r->output_channels);
}

void router_reschedule(Router *r)
{
    if (r->reschedule_next_tick) {
        r->eventq->reschedule(1, tick_event_from_id(r->id));
    }
}

// Returns an stb array containing the series of routed output ports.
int *source_route_compute(TopoDesc td, int src_id, int dst_id)
{
    int *path = NULL;
    int total = td.k;
    int cw_dist = (dst_id - src_id + total) % total;
    if (cw_dist <= total / 2) {
        // Clockwise
        for (int i = 0; i < cw_dist; i++)
            arrput(path, 2);
        arrput(path, 0);
    } else {
        // Counterclockwise
        // TODO: if CW == CCW, pick random
        for (int i = 0; i < total - cw_dist; i++)
            arrput(path, 1);
        arrput(path, 0);
    }
    printf("Source route computation: %d -> %d : {", src_id, dst_id);
    for (long i = 0; i < arrlen(path); i++)
        printf("%d,", path[i]);
    printf("}\n");
    return path;
}

// Tick a router. This function does all of the work that a router has to
// process in a single cycle, i.e. all of its pipeline stages and statistics
// update. This simplifies the event system by streamlining event types into a
// single one, and making the order between them the only thing for us to
// consider.
void router_tick(Router *r)
{
    // Make sure this router has not been already ticked in this cycle.
    if (r->eventq->curr_time() == r->last_tick) {
        // dbg() << "WARN: double tick! curr_time=" << eventq->curr_time()
        //       << ", last_tick=" << last_tick << std::endl;
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
        // bool empty = true;
        // for (int i = 0; i < radix; i++) {
        //     if (!input_units[i].buf.empty()) {
        //         empty = false;
        //         break;
        //     }
        // }
        // if (!empty) {
        //     reschedule_next_tick = true;
        // }
    }

    // Update the global state of each input/output unit.
    update_states(r);

    // Do the rescheduling at here once to prevent flooding the event queue.
    router_reschedule(r);

    r->last_tick = r->eventq->curr_time();
}

///
/// Pipeline stages
///

void source_generate(Router *r)
{
    OutputUnit *ou = &r->output_units[0];

    if (ou->credit_count <= 0) {
        dprintf(r, "Credit stall!\n");
        return;
    }

    // Handle flit_h = zalloc(flit_allocator);
    // Flit *flit = zptr(flit_h);
    Flit *flit = flit_create(FLIT_BODY, r->id.value, (r->id.value + 2) % 4,
                             r->flit_payload_counter);
    if (r->flit_payload_counter == 0) {
        flit->type = FLIT_HEAD;
        flit->route_info.path = source_route_compute(
            r->top_desc, flit->route_info.src, flit->route_info.dst);
        r->flit_payload_counter++;
    } else if (r->flit_payload_counter == 3 /* FIXME hardcoded packet size */) {
        flit->type = FLIT_TAIL;
        r->flit_payload_counter = 0;
    } else {
        r->flit_payload_counter++;
    }

    assert(r->radix == 1);
    Channel *out_ch = r->output_channels[0];
    out_ch->put(flit);

    dprintf(r, "Credit decrement, credit=%d->%d\n", ou->credit_count,
            ou->credit_count - 1);
    ou->credit_count--;
    assert(ou->credit_count >= 0);

    r->flit_gen_count++;

    char s[IDSTRLEN];
    dprintf(r, "Flit created and sent: %s\n", flit_str(flit, s));

    // TODO: for now, infinitely generate flits.
    r->reschedule_next_tick = true;
}

void destination_consume(Router *r)
{
    InputUnit *iu = &r->input_units[0];

    if (!queue_empty(iu->buf)) {
        Flit *flit = queue_front(iu->buf);
        char s[IDSTRLEN];
        dprintf(r, "Destination buf size=%zd\n", queue_len(iu->buf));
        dprintf(r, "Flit arrived: %s\n", flit_str(flit, s));
        flit_destroy(flit);

        r->flit_arrive_count++;
        queue_pop(iu->buf);
        assert(queue_empty(iu->buf));

        Channel *in_ch = r->input_channels[0];
        in_ch->put_credit(Credit{});

        RouterPortPair src_pair = in_ch->conn.src;
        dprintf(r, "Credit sent to {%s, %d}\n", id_str(src_pair.id, s),
                src_pair.port);

        // Self-tick autonomously unless all input ports are empty.
        r->reschedule_next_tick = true;
    }
}

void fetch_flit(Router *r)
{
    for (int iport = 0; iport < r->radix; iport++) {
        Channel *ich = r->input_channels[iport];
        InputUnit *iu = &r->input_units[iport];
        Flit *flit = ich->get();
        if (!flit) continue;

        char s[IDSTRLEN];
        dprintf(r, "Fetched flit %s, buf[%d].size()=%zd\n",
                flit_str(flit, s), iport, queue_len(iu->buf));

        // If the buffer was empty, this is the only place to kickstart the
        // pipeline.
        if (queue_empty(iu->buf)) {
            // dprintf(r, "fetch_flit: buf was empty\n");
            // If the input unit state was also idle (empty != idle!), set
            // the stage to RC.
            if (iu->next_global == STATE_IDLE) {
                // Idle -> RC transition
                iu->next_global = STATE_ROUTING;
                iu->stage = PIPELINE_RC;
            }

            r->reschedule_next_tick = true;
            }

            queue_put(iu->buf, flit);

            assert((size_t)queue_len(iu->buf) <= r->input_buf_size &&
                   "Input buffer overflow!");
    }
}

void fetch_credit(Router *r)
{
    for (int oport = 0; oport < r->radix; oport++) {
        Channel *och = r->output_channels[oport];
        Credit c;
        bool got = och->get_credit(&c);
        if (got) {
            OutputUnit *ou = &r->output_units[oport];
            dprintf(r, "Fetched credit, oport=%d\n", oport);
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
            dprintf(r, "Credit update! credit=%d->%d (oport=%d)\n",
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
                // dprintf(r, "credit update with kickstart! (iport=%d)\n",
                //         ou->input_port);
            } else {
                // dprintf(r, "credit update, but no kickstart (credit=%d)\n",
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
            Flit *flit = queue_front(iu->buf);
            char s[IDSTRLEN];
            dprintf(r, "Route computation: %s\n", flit_str(flit, s));
            assert(!queue_empty(iu->buf));

            // TODO: Simple algorithmic routing: keep rotating clockwise until
            // destination is met.
            // if (flit.route_info.dst == std::get<RtrId>(id).id) {
            //     // Port 0 is always connected to a terminal node
            //     iu->route_port = 0;
            // } else {
            //     int total = 4; /* FIXME: hardcoded */
            //     int cw_dist =
            //         (flit.route_info.dst - flit.route_info.src + total) %
            //         total;
            //     if (cw_dist <= total / 2) {
            //         // Clockwise is better
            //         iu->route_port = 2;
            //     } else {
            //         // TODO: if CW == CCW, pick random
            //         iu->route_port = 1;
            //     }
            // }

            assert(flit->route_info.idx < arrlenu(flit->route_info.path));
            dprintf(r, "RC: path size = %zd\n",
                    arrlen(flit->route_info.path));
            iu->route_port = flit->route_info.path[flit->route_info.idx];
            dprintf(r, "RC success for %s (idx=%zu, oport=%d)\n",
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
        dprintf(r, "VA: competing for oport %d from iports {", out_port);
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
            // dprintf(r, "SA: granted oport %d to iport %d\n", out_port, iport);
            r->sa_last_grant_input = iport;
            return iport;
        } else if (iu->stage == PIPELINE_SA && iu->route_port == out_port &&
                   iu->global == STATE_CREDWAIT) {
            dprintf(r, "Credit stall! port=%d\n", iu->route_port);
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
                dprintf(r, "VA: success for %s from iport %d to oport %d\n",
                        flit_str(queue_front(iu->buf), s), iport, oport);

                // We now have the VC, but we cannot proceed to the SA stage if
                // there is no credit.
                if (ou->credit_count == 0) {
                    dprintf(r, "VA: no credit, switching to CreditWait\n");
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
                dprintf(r, "SA success for %s from iport %d to oport %d\n",
                        flit_str(queue_front(iu->buf), s), iport, oport);

                // The flit leaves the buffer here.
                Flit *flit = queue_front(iu->buf);
                queue_pop(iu->buf);
                assert(!iu->st_ready);
                iu->st_ready = flit;

                // Credit decrement.
                dprintf(r, "Credit decrement, credit=%d->%d (oport=%d)\n",
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
                        // dprintf(this, "SA: next state is Idle\n");
                    } else {
                        iu->next_global = STATE_ROUTING;
                        iu->stage = PIPELINE_RC;
                        // dprintf(this, "SA: next state is Routing\n");
                    }
                    r->reschedule_next_tick = true;
                } else if (ou->credit_count == 0) {
                    dprintf(r, "SA: switching to CW\n");
                    iu->next_global = STATE_CREDWAIT;
                    ou->next_global = STATE_CREDWAIT;
                    // dprintf(this, "SA: next state is CreditWait\n");
                } else {
                    iu->next_global = STATE_ACTIVE;
                    iu->stage = PIPELINE_SA;
                    // dprintf(this, "SA: next state is Active\n");
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
            Channel *out_ch = r->output_channels[iu->route_port];
            out_ch->put(flit);
            RouterPortPair dst_pair = out_ch->conn.dst;

            char s[IDSTRLEN], s2[IDSTRLEN];
            dprintf(r, "Switch traverse: %s sent to {%s, %d}\n",
                    flit_str(flit, s), id_str(dst_pair.id, s2), dst_pair.port);

            // With output speedup:
            // auto &ou = output_units[iu->route_port];
            // ou->buf.push_back(flit);

            // CT stage: return credit to the upstream node.
            Channel *in_ch = r->input_channels[iport];
            in_ch->put_credit(Credit{});
            RouterPortPair src_pair = in_ch->conn.src;
            dprintf(r, "Credit sent to {%s, %d}\n", id_str(src_pair.id, s),
                    src_pair.port);
        }
    }
}

void update_states(Router *r)
{
    bool changed = false;
    for (int port = 0; port < r->radix; port++) {
        InputUnit *iu = &r->input_units[port];
        OutputUnit *ou = &r->output_units[port];
        if (iu->global != iu->next_global) {
            iu->global = iu->next_global;
            changed = true;
        }
        if (ou->global != ou->next_global) {
            assert(!(ou->next_global == STATE_CREDWAIT && ou->credit_count > 0));
            ou->global = ou->next_global;
            changed = true;
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
        Channel *ch = r->output_channels[i];
        printf("Channel[%d]: {", i);
        for (long i = queue_fronti(ch->buf); i != queue_backi(ch->buf);
             i = (i + 1) % queue_cap(ch->buf)) {
            TimedFlit tf = ch->buf[i];
            printf("%ld:%s,", tf.time, flit_str(tf.flit, s));
        }
        printf("}\n");
    }
}
