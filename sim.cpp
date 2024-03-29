#include "sim.h"
#include <stdio.h>
#include <assert.h>

void print_conn(const char *name, Connection conn);

Sim::Sim(bool verbose_mode, int debug_mode, Topology top, int terminal_count,
         int router_count, int radix, int vc_count, double mean_interval, long input_buf_size)
    : debug_mode(debug_mode), topology(top), traffic_desc(terminal_count),
      rand_gen(terminal_count, mean_interval)
{
    // Tornado pattern for 4-ring
    // traffic_desc = {TRF_DESIGNATED, std::vector<int>(terminal_count)};
    // traffic_desc.dests[0] = 2;
    // traffic_desc.dests[1] = 3;
    // traffic_desc.dests[2] = 0;
    // traffic_desc.dests[3] = 1;

    // VC vs. Wormhole pattern (6-ary 2-torus)
    // traffic_desc = {TRF_DESIGNATED, std::vector<int>(terminal_count)};
    // traffic_desc.dests[19] = 22;
    // traffic_desc.dests[20] = 15;

    channel_delay = 1; /* FIXME hardcoded */
    packet_len = 4; /* FIXME hardcoded */

    // Initialize the event system
    eventq_init(&eventq);

    // Initialize channels
    channels.reserve(hmlen(top.forward_hash));
    for (ptrdiff_t i = 0; i < hmlen(top.forward_hash); i++) {
        Connection conn = top.forward_hash[i].value;
        // printf("Found connection: %d.%d.%d -> %d.%d.%d\n", conn.src.id.type, conn.src.id.value,
        //        conn.src.port, conn.dst.id.type, conn.dst.id.value, conn.dst.port);
        channels.emplace_back(&eventq, channel_delay, conn);
        // channels.emplace_back(&eventq, channel_delay, conn);
    }
    channel_map = NULL;
    for (size_t i = 0; i < channels.size(); i++) {
        Channel *ch = &channels[i];
        hmput(channel_map, ch->conn.uniq, ch);
    }

    // Initialize terminal nodes
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
        long src_idx = hmgeti(channel_map, src_conn.uniq);
        long dst_idx = hmgeti(channel_map, dst_conn.uniq);
        assert(src_idx >= 0);
        assert(dst_idx >= 0);
        Channel *src_out_ch = channel_map[src_idx].value;
        Channel *dst_in_ch = channel_map[dst_idx].value;

        arrput(src_out_chs, src_out_ch);
        arrput(dst_in_chs, dst_in_ch);

        src_nodes.push_back(std::make_unique<Router>(
            *this, &eventq, &stat, verbose_mode, src_id(id), 1, vc_count, top.desc,
            traffic_desc, rand_gen, packet_len, src_in_chs, src_out_chs,
            input_buf_size));
        dst_nodes.push_back(std::make_unique<Router>(
            *this, &eventq, &stat, verbose_mode, dst_id(id), 1, vc_count, top.desc,
            traffic_desc, rand_gen, packet_len, dst_in_chs, dst_out_chs,
            input_buf_size));

        arrfree(src_in_chs);
        arrfree(src_out_chs);
        arrfree(dst_in_chs);
        arrfree(dst_out_chs);
    }

    // Initialize router nodes
    for (int id = 0; id < router_count; id++) {
        Channel **in_chs = NULL;
        Channel **out_chs = NULL;

        for (int port = 0; port < radix; port++) {
            RouterPortPair rpp = {rtr_id(id), port};
            Connection output_conn = conn_find_forward(&top, rpp);
            Connection input_conn = conn_find_reverse(&top, rpp);
            assert(output_conn.src.port != -1);
            assert(input_conn.src.port != -1);
            long out_idx = hmgeti(channel_map, output_conn.uniq);
            long in_idx = hmgeti(channel_map, input_conn.uniq);
            assert(out_idx >= 0);
            assert(in_idx >= 0);
            Channel *out_ch = channel_map[out_idx].value;
            Channel *in_ch = channel_map[in_idx].value;

            arrput(out_chs, out_ch);
            arrput(in_chs, in_ch);
        }

        routers.push_back(std::make_unique<Router>(
            *this, &eventq, &stat, verbose_mode, rtr_id(id), radix, vc_count,
            top.desc, traffic_desc, rand_gen, packet_len, in_chs, out_chs,
            input_buf_size));

        arrfree(in_chs);
        arrfree(out_chs);
    }
}

void sim_run_until(Sim *sim, long until)
{
    long last_print_cycle = 0;

    while (!eventq_empty(&sim->eventq)) {
        // Terminate simulation if the specified time is expired
        if (0 <= until && until < next_time(&sim->eventq)) {
            break;
        }
        Event e = eventq_pop(&sim->eventq);
        if (sim->eventq.curr_time() != last_print_cycle &&
            sim->eventq.curr_time() % 100 == 0) {
            printf("[@%3ld/%3ld]\n", sim->eventq.curr_time(), until);
            last_print_cycle = sim->eventq.curr_time();
        }
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
        for (size_t i = 0; i < sim->routers.size(); i++) {
            router_print_state(sim->routers[i].get());
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

void channel_xy_load(Sim *sim) {
    // Determine the direction of this channel.
    for (auto &ch : sim->channels) {
        auto src = ch.conn.src.id;
        auto dst = ch.conn.dst.id;

        // Skip terminal channels.
        if (is_src(src) || is_dst(src) || is_src(dst) || is_dst(dst)) {
            continue;
        }

        int dimension = 0;
        for (; dimension < sim->topology.desc.r; dimension++) {
            int src_id = torus_id_xyz_get(ch.conn.src.id.value, sim->topology.desc.k, dimension);
            int dst_id = torus_id_xyz_get(ch.conn.dst.id.value, sim->topology.desc.k, dimension);
            if (src_id == dst_id) {
                break;
            }
        }
        printf("channel direction=%d, load=%ld\n", dimension, ch.load_count);
    }
}

void sim_report(Sim *sim) {
    char s[IDSTRLEN];

    // Sample router for fetching topology info.
    assert(!sim->routers.empty());
    Router &r = *sim->routers[0].get();

    printf("\n");
    printf("==== SIMULATION RESULT ====\n");

    printf("Topology: %d-ary %d-torus\n", sim->topology.desc.k, sim->topology.desc.r); 
    printf("Radix: %d\n", r.radix); 
    printf("# of VCs per channel: %d\n", r.vc_count); 
    printf("# of total cycle: %ld\n", curr_time(&sim->eventq));
    printf("# of double ticks: %ld\n", sim->stat.double_tick_count);
    printf("\n");

    for (size_t i = 0; i < sim->src_nodes.size(); i++) {
        Router *src = sim->src_nodes[i].get();
        printf("[%s] ", id_str(src->id, s));
        printf("# of flits departed: %ld\n", src->flit_depart_count);
    }

    for (size_t i = 0; i < sim->dst_nodes.size(); i++) {
        Router *dst = sim->dst_nodes[i].get();
        printf("[%s] ", id_str(dst->id, s));
        printf("# of flits arrived: %ld\n", dst->flit_arrive_count);
    }

    printf("\n");

    float interval_avg = static_cast<float>(curr_time(&sim->eventq)) /
                         (static_cast<float>(sim->stat.packet_gen_count) /
                          static_cast<float>(sim->src_nodes.size()));
    printf("Average interval: %lf cycles\n", interval_avg);
    float hop_count_avg = static_cast<float>(sim->stat.hop_count_sum) /
                          static_cast<float>(sim->stat.packet_gen_count);
    printf("Average hop count: %lf hops\n", hop_count_avg);
    float latency_avg = static_cast<float>(sim->stat.latency_sum) /
                        static_cast<float>(sim->stat.packet_arrive_count);
    printf("Average latency: %lf\n", latency_avg);

    // channel_xy_load(sim);
}

// Process an event.
void sim_process(Sim *sim, Event e)
{
    if (is_src(e.id)) {
        e.f(sim->src_nodes[e.id.value].get());
    } else if (is_dst(e.id)) {
        e.f(sim->dst_nodes[e.id.value].get());
    } else if (is_rtr(e.id)) {
        e.f(sim->routers[e.id.value].get());
    } else {
        assert(0);
    }
}

void sim_destroy(Sim *sim)
{
    hmfree(sim->channel_map);

    // Stat
    eventq_destroy(&sim->eventq);
}
