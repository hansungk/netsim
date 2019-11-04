#include "sim.h"
#include <cassert>
#include <iostream>

static void print_channel(const char *ch_name, Channel *ch)
{
  printf("%s: %d.%d.%d -> %d.%d.%d\n", ch_name, ch->src.id.type,
         ch->src.id.value, ch->src.port, ch->dst.id.type, ch->dst.id.value,
         ch->dst.port);
}

Sim::Sim(int terminal_count, int router_count, int radix, Topology &top)
    : topology(top)
{
  // Initialize channels
  for (ptrdiff_t i = 0; i < hmlen(top.forward_hash); i++) {
    Connection conn = top.forward_hash[i].value;
    // printf("Found connection: %d.%d.%d -> %d.%d.%d\n", conn.src.id.type, conn.src.id.value,
    //        conn.src.port, conn.dst.id.type, conn.dst.id.value, conn.dst.port);
    channels.emplace_back(eventq, channel_delay, conn.src, conn.dst);
  }
  for (auto &ch : channels) {
    auto conn = Connection{ch.src, ch.dst};
    channel_map.insert({conn, ch});
  }

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
    Connection src_conn = topology.find_forward(src_rpp);
    Connection dst_conn = topology.find_reverse(dst_rpp);
    assert(src_conn.src.port != -1);
    assert(dst_conn.src.port != -1);
    auto src_out_ch_it = channel_map.find(src_conn);
    auto dst_in_ch_it = channel_map.find(dst_conn);
    assert(src_out_ch_it != channel_map.end());
    assert(dst_in_ch_it != channel_map.end());
    Channel *src_out_ch = &src_out_ch_it->second;
    Channel *dst_in_ch = &dst_in_ch_it->second;

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
      Connection output_conn = topology.find_forward(rpp);
      Connection input_conn = topology.find_reverse(rpp);
      assert(output_conn.src.port != -1);
      assert(input_conn.src.port != -1);
      auto out_ch_it = channel_map.find(output_conn);
      auto in_ch_it = channel_map.find(input_conn);
      assert(out_ch_it != channel_map.end());
      assert(in_ch_it != channel_map.end());
      Channel *out_ch = &out_ch_it->second;
      Channel *in_ch = &in_ch_it->second;

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
