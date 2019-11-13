#include "router.h"
#include "queue.h"
#include "stb_ds.h"
#include <stdio.h>
#include <assert.h>

// Maximum supported torus dimension.
#define NORMALLEN 10

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
    queue_init(ch.buf, 4); // FIXME hardcoded
    queue_init(ch.buf_credit, 4); // FIXME hardcoded
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
    queue_put(ch->buf, tf);
    reschedule(ch->eventq, ch->delay, tick_event_from_id(ch->conn.dst.id));
}

void channel_put_credit(Channel *ch, Credit credit)
{
    TimedCredit tc = {curr_time(ch->eventq) + ch->delay, credit};
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

int topology_connect(Topology *t, RouterPortPair input, RouterPortPair output)
{
    int old_output_i = hmgeti(t->forward_hash, input);
    int old_input_i = hmgeti(t->reverse_hash, output);
    if (old_output_i >= 0 || old_input_i >= 0) {
        RouterPortPair old_output = t->forward_hash[old_output_i].value.dst;
        RouterPortPair old_input = t->reverse_hash[old_input_i].value.src;
        if (input.id.type == old_input.id.type &&
            input.id.value == old_input.id.value &&
            input.port == old_input.port &&
            output.id.type == old_output.id.type &&
            output.id.value == old_output.id.value &&
            output.port == old_output.port) {
            // Already connected.
            return 1;
        } else {
            // Bad connectivity: source or destination port is already connected
            return 0;
        }
    }
    int uniq = hmlen(t->forward_hash);
    Connection conn = (Connection){.src = input, .dst = output, .uniq = uniq};
    hmput(t->forward_hash, input, conn);
    hmput(t->reverse_hash, output, conn);
    assert(hmgeti(t->forward_hash, input) >= 0);
    return 1;
}

int topology_connect_terminals(Topology *t, const int *ids)
{
    int res = 1;
    for (int i = 0; i < arrlen(ids); i++) {
        RouterPortPair src_port = {src_id(ids[i]), 0};
        RouterPortPair dst_port = {dst_id(ids[i]), 0};
        RouterPortPair rtr_port = {rtr_id(ids[i]), 0};

        // Bidirectional channel
        res &= topology_connect(t, src_port, rtr_port);
        res &= topology_connect(t, rtr_port, dst_port);
        if (!res)
            return 0;
    }
    return 1;
}

// Port usage: 0:terminal, 1:counter-clockwise, 2:clockwise
int topology_connect_ring(Topology *t, const int *ids, int direction)
{
    printf("Connecting ring: {");
    for (long i = 0; i < arrlen(ids); i++)
        printf("%d,", ids[i]);
    printf("}\n");

    int port_cw = direction * 2 + 2;
    int port_ccw = direction * 2 + 1;
    int res = 1;
    for (long i = 0; i < arrlen(ids); i++) {
        int l = ids[i];
        int r = ids[(i + 1) % arrlen(ids)];
        RouterPortPair lport = {rtr_id(l), port_cw};
        RouterPortPair rport = {rtr_id(r), port_ccw};

        // Bidirectional channel
        res &= topology_connect(t, lport, rport);
        res &= topology_connect(t, rport, lport);
        if (!res)
            return 0;
    }
    return 1;
}

Topology topology_ring(int n)
{
    Topology top = topology_create();
    int *ids = NULL;
    int res = 1;

    for (int id = 0; id < n; id++)
        arrput(ids, id);

    // Inter-router channels
    res &= topology_connect_ring(&top, ids, 0);
    // Terminal node channels
    res &= topology_connect_terminals(&top, ids);
    assert(res);

    arrfree(ids);
    return top;
}

// Connects part of the torus that corresponds to the the given parameters.
// Calls itself recursively to form the desired connection.
//
// dimension: size of the 'normal' array.
// offset: offset of the lowest index.
int topology_connect_torus_dimension(Topology *t, int k, int r, int dimension, int *normal, int offset)
{
    printf("Normal=[%d,%d,%d]\n", normal[0], normal[1], normal[2]);

    int res = 1;
    int zeros = 0;
    for (int i = 0; i < dimension; i++)
        if (normal[i] == 0)
            zeros++;

    if (zeros == 1) {
        // Ring
        int stride = 1;
        for (int i = 0; i < dimension; i++, stride *= k) {
            if (normal[i] == 0) {
                int *ids = NULL;
                for (int j = 0; j < k; j++)
                    arrput(ids, offset + j * stride);
                res &= topology_connect_ring(t, ids, i);
                arrfree(ids);
                break; // only one 0 in normal
            }
        }
    } else {
        int stride = 1;
        for (int i = 0; i < dimension; i++, stride *= k) {
            if (normal[i] == 0) {
                int subnormal[NORMALLEN];
                memcpy(subnormal, normal, dimension * sizeof(int));
                subnormal[i] = 1; // lock this dimension
                for (int j = 0; j < k; j++) {
                    int suboffset = offset + j * stride;
                    res &= topology_connect_torus_dimension(
                        t, k, r, dimension, subnormal, suboffset);
                }
            }
        }
    }

    return res;
}

Topology topology_torus(int k, int r)
{
    int normal[NORMALLEN] = {0};
    Topology top = topology_create();
    int res = topology_connect_torus_dimension(&top, k, r, r, normal, 0);
    assert(res);
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

Flit *flit_create(enum FlitType t, int src, int dst, long p)
{
    Flit *flit = (Flit *)malloc(sizeof(Flit));
    RouteInfo ri = {src, dst, NULL, 0};
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
        reschedule(r->eventq, 1, tick_event_from_id(r->id));
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
    if (curr_time(r->eventq) == r->last_tick) {
        // dbg() << "WARN: double tick! curr_time=" << curr_time(eventq)
        //       << ", last_tick=" << last_tick << std::endl;
        r->stat->double_tick_count++;
        return;
    }

    r->reschedule_next_tick = 0;

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

    if (ou->credit_count <= 0) {
        debugf(r, "Credit stall!\n");
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
    Channel *och = r->output_channels[0];
    channel_put(och, flit);

    debugf(r, "Credit decrement, credit=%d->%d\n", ou->credit_count,
           ou->credit_count - 1);
    ou->credit_count--;
    assert(ou->credit_count >= 0);

    r->flit_gen_count++;

    char s[IDSTRLEN];
    debugf(r, "Flit created and sent: %s\n", flit_str(flit, s));

    // TODO: for now, infinitely generate flits.
    r->reschedule_next_tick = 1;
}

void destination_consume(Router *r)
{
    InputUnit *iu = &r->input_units[0];

    if (!queue_empty(iu->buf)) {
        Flit *flit = queue_front(iu->buf);
        char s[IDSTRLEN];
        debugf(r, "Destination buf size=%zd\n", queue_len(iu->buf));
        debugf(r, "Flit arrived: %s\n", flit_str(flit, s));
        flit_destroy(flit);

        r->flit_arrive_count++;
        queue_pop(iu->buf);
        assert(queue_empty(iu->buf));

        Channel *ich = r->input_channels[0];
        channel_put_credit(ich, (Credit){});

        RouterPortPair src_pair = ich->conn.src;
        debugf(r, "Credit sent to {%s, %d}\n", id_str(src_pair.id, s),
               src_pair.port);

        // Self-tick autonomously unless all input ports are empty.
        r->reschedule_next_tick = 1;
    }
}

void fetch_flit(Router *r)
{
    for (int iport = 0; iport < r->radix; iport++) {
        Channel *ich = r->input_channels[iport];
        InputUnit *iu = &r->input_units[iport];
        Flit *flit = channel_get(ich);
        if (!flit) continue;

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

            r->reschedule_next_tick = 1;
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
        int got = channel_get_credit(och, &c);
        if (got) {
            OutputUnit *ou = &r->output_units[oport];
            debugf(r, "Fetched credit, oport=%d\n", oport);
            // In any time, there should be at most 1 credit in the buffer.
            assert(queue_empty(ou->buf_credit));
            queue_put(ou->buf_credit, c);
            r->reschedule_next_tick = 1;
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
                r->reschedule_next_tick = 1;
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
            Flit *flit = queue_front(iu->buf);
            char s[IDSTRLEN];
            debugf(r, "Route computation: %s\n", flit_str(flit, s));
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
            debugf(r, "RC: path size = %zd\n", arrlen(flit->route_info.path));
            iu->route_port = flit->route_info.path[flit->route_info.idx];
            debugf(r, "RC success for %s (idx=%zu, oport=%d)\n",
                   flit_str(flit, s), flit->route_info.idx, iu->route_port);
            flit->route_info.idx++;

            // RC -> VA transition
            iu->next_global = STATE_VCWAIT;
            iu->stage = PIPELINE_VA;
            r->reschedule_next_tick = 1;
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
                r->reschedule_next_tick = 1;
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
                    r->reschedule_next_tick = 1;
                } else if (ou->credit_count == 0) {
                    debugf(r, "SA: switching to CW\n");
                    iu->next_global = STATE_CREDWAIT;
                    ou->next_global = STATE_CREDWAIT;
                    // debugf(this, "SA: next state is CreditWait\n");
                } else {
                    iu->next_global = STATE_ACTIVE;
                    iu->stage = PIPELINE_SA;
                    // debugf(this, "SA: next state is Active\n");
                    r->reschedule_next_tick = 1;
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
    if (changed) r->reschedule_next_tick = 1;
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
