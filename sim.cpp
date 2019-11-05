#include "sim.h"
#include <cassert>
#include <iostream>

void print_conn(const char *name, Connection conn);

Sim::Sim(int terminal_count, int router_count, int radix, Topology &top)
    : topology(top)
{
  // Initialize channels
  for (ptrdiff_t i = 0; i < hmlen(top.forward_hash); i++) {
    Connection conn = top.forward_hash[i].value;
    // printf("Found connection: %d.%d.%d -> %d.%d.%d\n", conn.src.id.type, conn.src.id.value,
    //        conn.src.port, conn.dst.id.type, conn.dst.id.value, conn.dst.port);
    channels.emplace_back(eventq, channel_delay, conn);
  }
  channel_map = NULL;
  for (auto &ch : channels)
    hmput(channel_map, ch.conn.uniq, &ch);

  TopoDesc td{TOP_TORUS, 4, 1};

  // Initialize terminal nodes
  for (int id = 0; id < terminal_count; id++) {
    // Terminal nodes only have a single port.  Also, destination nodes
    // doesn't have output ports!
    std::vector<Channel *> src_in_chs; // empty
    std::vector<Channel *> src_out_chs;
    std::vector<Channel *> dst_in_chs;
    std::vector<Channel *> dst_out_chs; // empty

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

    src_out_chs.push_back(src_out_ch);
    dst_in_chs.push_back(dst_in_ch);

    src_nodes.emplace_back(eventq, stat, td, src_id(id), 1, src_in_chs,
                           src_out_chs);
    dst_nodes.emplace_back(eventq, stat, td, dst_id(id), 1, dst_in_chs,
                           dst_out_chs);
  }

  // Initialize router nodes
  for (int id = 0; id < router_count; id++) {
    std::vector<Channel *> in_chs;
    std::vector<Channel *> out_chs;

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

      out_chs.push_back(out_ch);
      in_chs.push_back(in_ch);
    }

    routers.emplace_back(eventq, stat, td, rtr_id(id), radix, in_chs, out_chs);
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

    report();
}

void Sim::report() const {
    std::cout << std::endl;
    std::cout << "==== SIMULATION RESULT ====\n";

    std::cout << "# of ticks: " << eventq.curr_time() << std::endl;
    std::cout << "# of double ticks: " << stat.double_tick_count << std::endl;
    std::cout << std::endl;

    for (auto &src : src_nodes) {
        std::cout << "[" << src.id << "] ";
        std::cout << "# of flits generated: " << src.flit_gen_count << std::endl;
    }

    for (auto &dst : dst_nodes) {
        std::cout << "[" << dst.id << "] ";
        std::cout << "# of flits arrived: " << dst.flit_arrive_count << std::endl;
    }
}

void Sim::process(const Event &e) {
    if (is_src(e.id)) {
        e.f(src_nodes[e.id.value]);
    } else if (is_dst(e.id)) {
        e.f(dst_nodes[e.id.value]);
    } else if (is_rtr(e.id)){
        e.f(routers[e.id.value]);
    } else {
        assert(false);
    }
}

void sim_destroy(Sim *sim)
{
  hmfree(sim->channel_map);
}
