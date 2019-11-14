#include "sim.h"
#include <stdio.h>
#include <assert.h>

void print_conn(const char *name, Connection conn);

Sim sim_create(int debug_mode, int terminal_count, int router_count, int radix,
               Topology top)
{
    Sim sim;
    memset(&sim, 0, sizeof(Sim));
    sim.flit_allocator = alloc_create(sizeof(Flit));
    sim.debug_mode = debug_mode;
    sim.topology = top;
    sim.channel_delay = 1; /* FIXME hardcoded */

    // Initialize event system
    eventq_init(&sim.eventq);

    // Initialize channels
    sim.channels = NULL;
    for (ptrdiff_t i = 0; i < hmlen(top.forward_hash); i++) {
        Connection conn = top.forward_hash[i].value;
        // printf("Found connection: %d.%d.%d -> %d.%d.%d\n", conn.src.id.type, conn.src.id.value,
        //        conn.src.port, conn.dst.id.type, conn.dst.id.value, conn.dst.port);
        Channel ch = channel_create(&sim.eventq, sim.channel_delay, conn);
        arrput(sim.channels, ch);
        // channels.emplace_back(&eventq, sim.channel_delay, conn);
    }
    sim.channel_map = NULL;
    for (long i = 0; i < arrlen(sim.channels); i++) {
        Channel *ch = &sim.channels[i];
        hmput(sim.channel_map, ch->conn.uniq, ch);
    }

    // FIXME This is not stored in Sim.
    TopoDesc td = {TOP_TORUS, 4, 1};

    // Initialize terminal nodes
    sim.src_nodes = NULL;
    sim.dst_nodes = NULL;
    for (int id = 0; id < terminal_count; id++) {
        // Terminal nodes only have a single port.  Also, destination nodes
        // doesn't have output ports!
        Channel **src_in_chs = NULL; // empty
        Channel **src_out_chs = NULL;
        Channel **dst_in_chs = NULL;
        Channel **dst_out_chs = NULL; // empty

        RouterPortPair src_rpp = {src_id(id), 0};
        RouterPortPair dst_rpp = {dst_id(id), 0};
        Connection src_conn = conn_find_forward(&top, src_rpp);
        Connection dst_conn = conn_find_reverse(&top, dst_rpp);
        assert(src_conn.src.port != -1 && "Source is not connected!");
        assert(dst_conn.src.port != -1 && "Destination is not connected!");
        long src_idx = hmgeti(sim.channel_map, src_conn.uniq);
        long dst_idx = hmgeti(sim.channel_map, dst_conn.uniq);
        assert(src_idx >= 0);
        assert(dst_idx >= 0);
        Channel *src_out_ch = sim.channel_map[src_idx].value;
        Channel *dst_in_ch = sim.channel_map[dst_idx].value;

        arrput(src_out_chs, src_out_ch);
        arrput(dst_in_chs, dst_in_ch);

        Router src_node = router_create(&sim.eventq, sim.flit_allocator, &sim.stat, td,
                                        src_id(id), 1, src_in_chs, src_out_chs);
        Router dst_node = router_create(&sim.eventq, sim.flit_allocator, &sim.stat, td,
                                        dst_id(id), 1, dst_in_chs, dst_out_chs);
        arrput(sim.src_nodes, src_node);
        arrput(sim.dst_nodes, dst_node);

        arrfree(src_in_chs);
        arrfree(src_out_chs);
        arrfree(dst_in_chs);
        arrfree(dst_out_chs);
    }

    // Initialize router nodes
    sim.routers = NULL;

    for (int id = 0; id < router_count; id++) {
        Channel **in_chs = NULL;
        Channel **out_chs = NULL;

        for (int port = 0; port < radix; port++) {
            RouterPortPair rpp = {rtr_id(id), port};
            Connection output_conn = conn_find_forward(&top, rpp);
            Connection input_conn = conn_find_reverse(&top, rpp);
            assert(output_conn.src.port != -1);
            assert(input_conn.src.port != -1);
            long out_idx = hmgeti(sim.channel_map, output_conn.uniq);
            long in_idx = hmgeti(sim.channel_map, input_conn.uniq);
            assert(out_idx >= 0);
            assert(in_idx >= 0);
            Channel *out_ch = sim.channel_map[out_idx].value;
            Channel *in_ch = sim.channel_map[in_idx].value;

            arrput(out_chs, out_ch);
            arrput(in_chs, in_ch);
        }

        arrput(sim.routers, router_create(&sim.eventq, sim.flit_allocator, &sim.stat, td,
                                      rtr_id(id), radix, in_chs, out_chs));

        arrfree(in_chs);
        arrfree(out_chs);
    }

    return sim;
}

void sim_run_until(Sim *sim, long until)
{
    while (!eventq_empty(&sim->eventq)) {
        // Terminate simulation if the specified time is expired
        if (0 <= until && until < next_time(&sim->eventq))
            break;
        Event e = eventq_pop(&sim->eventq);
        sim_process(sim, e);
    }
}

// Returns 1 if the simulation is NOT terminated, 0 otherwise.
int sim_debug_step(Sim *sim)
{
    char line[1024] = {0};

    printf("(@%ld) > ", curr_time(&sim->eventq));
    if (fgets(line, 100, stdin) == NULL)
        return 0;
    char *nl = strchr(line, '\n');
    if (nl) *nl = '\0';

    if (!strcmp(line, "q")) {
        return 0;
    } else if (strlen(line) == 0) {
        return 1;
    } else if (!strcmp(line, "n")) {
        long until = curr_time(&sim->eventq);
        sim_run_until(sim, until + 1);
        return 1;
    } else if (!strcmp(line, "p")) {
        for (long i = 0; i < arrlen(sim->routers); i++) {
            router_print_state(&sim->routers[i]);
        }
        return 1;
    }

    // Commands with arguments.
    char *tok = strtok(line, " ");
    if (!strcmp(tok, "c")) {
        tok = strtok(NULL, " ");
        if (!tok) {
            printf("No argument given.\n");
            return 1;
        }
        char *endptr;
        long until = strtol(tok, &endptr, 10);
        if (*endptr != '\0') {
            printf("Invalid command.\n");
            return 1;
        }
        sim_run_until(sim, until);
        return 1;
    }

    // Command not understood.
    printf("Unknown command.\n");
    return 1;
}

// Run the simulator.
void sim_run(Sim *sim, long until)
{
    if (sim->debug_mode) {
        while (sim_debug_step(sim));
    } else {
        sim_run_until(sim, until);
    }
}

void sim_report(Sim *sim) {
    char s[IDSTRLEN];

    printf("\n");
    printf("==== SIMULATION RESULT ====\n");

    printf("# of ticks: %ld\n", curr_time(&sim->eventq));
    printf("# of double ticks: %ld\n", sim->stat.double_tick_count);
    printf("\n");

    for (long i = 0; i < arrlen(sim->src_nodes); i++) {
        Router *src = &sim->src_nodes[i];
        printf("[%s] ", id_str(src->id, s));
        printf("# of flits generated: %ld\n", src->flit_gen_count);
    }

    for (long i = 0; i < arrlen(sim->dst_nodes); i++) {
        Router *dst = &sim->dst_nodes[i];
        printf("[%s] ", id_str(dst->id, s));
        printf("# of flits arrived: %ld\n", dst->flit_arrive_count);
    }
}

// Process an event.
void sim_process(Sim *sim, Event e)
{
    if (is_src(e.id)) {
        e.f(&sim->src_nodes[e.id.value]);
    } else if (is_dst(e.id)) {
        e.f(&sim->dst_nodes[e.id.value]);
    } else if (is_rtr(e.id)) {
        e.f(&sim->routers[e.id.value]);
    } else {
        assert(0);
    }
}

void sim_destroy(Sim *sim)
{
    hmfree(sim->channel_map);
    alloc_destroy(sim->flit_allocator);
    for (long i = 0; i < arrlen(sim->channels); i++)
        channel_destroy(&sim->channels[i]);
    arrfree(sim->channels);

    for (long i = 0; i < arrlen(sim->routers); i++)
        router_destroy(&sim->routers[i]);
    for (long i = 0; i < arrlen(sim->src_nodes); i++)
        router_destroy(&sim->src_nodes[i]);
    for (long i = 0; i < arrlen(sim->dst_nodes); i++)
        router_destroy(&sim->dst_nodes[i]);
    arrfree(sim->routers);
    arrfree(sim->src_nodes);
    arrfree(sim->dst_nodes);

    eventq_destroy(&sim->eventq);
}
