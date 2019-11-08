#include "sim.h"
#include <cassert>
#include <iostream>

void print_conn(const char *name, Connection conn);

Sim::Sim(int terminal_count, int router_count, int radix, Topology &top)
    : topology(top)
{
    flit_allocator = alloc_create(sizeof(Flit));

    // Initialize channels
    channels = NULL;
    for (ptrdiff_t i = 0; i < hmlen(top.forward_hash); i++) {
        Connection conn = top.forward_hash[i].value;
        // printf("Found connection: %d.%d.%d -> %d.%d.%d\n", conn.src.id.type, conn.src.id.value,
        //        conn.src.port, conn.dst.id.type, conn.dst.id.value, conn.dst.port);
        Channel ch{&eventq, channel_delay, conn};
        arrput(channels, ch);
        // channels.emplace_back(&eventq, channel_delay, conn);
    }
    channel_map = NULL;
    for (long i = 0; i < arrlen(channels); i++) {
        Channel *ch = &channels[i];
        hmput(channel_map, ch->conn.uniq, ch);
    }

    TopoDesc td{TOP_TORUS, 4, 1};

    // Initialize terminal nodes
    src_nodes = NULL;
    dst_nodes = NULL;
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
        assert(src_conn.src.port != -1);
        assert(dst_conn.src.port != -1);
        long src_idx = hmgeti(channel_map, src_conn.uniq);
        long dst_idx = hmgeti(channel_map, dst_conn.uniq);
        assert(src_idx >= 0);
        assert(dst_idx >= 0);
        Channel *src_out_ch = channel_map[src_idx].value;
        Channel *dst_in_ch = channel_map[dst_idx].value;

        arrput(src_out_chs, src_out_ch);
        arrput(dst_in_chs, dst_in_ch);

        Router src_node = Router(&eventq, flit_allocator, &stat, td, src_id(id),
                                 1, src_in_chs, src_out_chs);
        arrput(src_nodes, src_node);
        arrput(dst_nodes, Router(&eventq, flit_allocator, &stat, td, dst_id(id), 1,
                                 dst_in_chs, dst_out_chs));

        arrfree(src_in_chs);
        arrfree(src_out_chs);
        arrfree(dst_in_chs);
        arrfree(dst_out_chs);
    }

    // Initialize router nodes
    routers = NULL;

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

        arrput(routers, Router(&eventq, flit_allocator, &stat, td, rtr_id(id),
                               radix, in_chs, out_chs));

        arrfree(in_chs);
        arrfree(out_chs);
    }
}

void Sim::run(long until) {
    while (!eventq.empty()) {
        auto e = eventq.pop();
        // Terminate simulation if the specified time is expired
        if (0 < until && until < eventq.curr_time()) {
            break;
        }
        process(e);
    }
}

void Sim::report() const {
    std::cout << std::endl;
    std::cout << "==== SIMULATION RESULT ====\n";

    std::cout << "# of ticks: " << eventq.curr_time() << std::endl;
    std::cout << "# of double ticks: " << stat.double_tick_count << std::endl;
    std::cout << std::endl;

    for (long i = 0; i < arrlen(src_nodes); i++) {
        Router *src = &src_nodes[i];
        std::cout << "[" << src->id << "] ";
        std::cout << "# of flits generated: " << src->flit_gen_count << std::endl;
    }

    for (long i = 0; i < arrlen(dst_nodes); i++) {
        Router *dst = &dst_nodes[i];
        std::cout << "[" << dst->id << "] ";
        std::cout << "# of flits arrived: " << dst->flit_arrive_count << std::endl;
    }
}

void Sim::process(const Event &e)
{
    if (is_src(e.id)) {
        e.f(src_nodes[e.id.value]);
    } else if (is_dst(e.id)) {
        e.f(dst_nodes[e.id.value]);
    } else if (is_rtr(e.id)) {
        e.f(routers[e.id.value]);
    } else {
        assert(false);
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
}
