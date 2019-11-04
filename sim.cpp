#include "sim.h"
#include <cassert>
#include <iostream>

Sim::Sim(int terminal_count, int router_count, int radix, Topology &top)
    : topology(top) {
    // Initialize channels
    int id = 0;
    for (auto conn : top.get_forward_map()) {
        channels.emplace_back(eventq, ChId{id}, channel_delay, conn.first,
                              conn.second);
        id++;
    }
    for (auto &ch : channels) {
        auto conn = std::pair{ch.src, ch.dst};
        channel_map.insert({conn, ch});
    }

    TopoDesc td{TOP_TORUS, 4, 1};

    // Initialize terminal nodes
    for (int id = 0; id < terminal_count; id++) {
        // Terminal nodes only have a single port.  Also, destination nodes
        // doesn't have output ports!
        ChannelRefVec src_input_channels; // empty
        ChannelRefVec src_output_channels;
        ChannelRefVec dst_input_channels;
        ChannelRefVec dst_output_channels; // empty

        RouterPortPair src_rpp = {SrcId{id}, 0};
        RouterPortPair dst_rpp = {DstId{id}, 0};
        auto src_downstream_input = topology.find_forward(src_rpp);
        auto dst_upstream_output = topology.find_reverse(dst_rpp);
        auto src_output_conn = std::pair{src_rpp, src_downstream_input};
        auto dst_input_conn = std::pair{dst_upstream_output, dst_rpp};
        auto &src_output_channel = channel_map.find(src_output_conn)->second;
        auto &dst_input_channel = channel_map.find(dst_input_conn)->second;

        src_output_channels.push_back(src_output_channel);
        dst_input_channels.push_back(dst_input_channel);

        src_nodes.emplace_back(eventq, stat, td, SrcId{id}, 1,
                               src_input_channels, src_output_channels);
        dst_nodes.emplace_back(eventq, stat, td, DstId{id}, 1,
                               dst_input_channels, dst_output_channels);
    }

    // Initialize router nodes
    for (int id = 0; id < router_count; id++) {
        ChannelRefVec input_channels;
        ChannelRefVec output_channels;

        for (int port = 0; port < radix; port++) {
            RouterPortPair rpp = {RtrId{id}, port};
            auto downstream_input = topology.find_forward(rpp);
            auto upstream_output = topology.find_reverse(rpp);
            auto output_conn = std::pair{rpp, downstream_input};
            auto input_conn = std::pair{upstream_output, rpp};
            auto &output_channel = channel_map.find(output_conn)->second;
            auto &input_channel = channel_map.find(input_conn)->second;

            output_channels.push_back(output_channel);
            input_channels.push_back(input_channel);
        }

        routers.emplace_back(eventq, stat, td, RtrId{id}, radix, input_channels,
                             output_channels);
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
    if (std::holds_alternative<SrcId>(e.id)) {
        e.f(src_nodes[std::get<SrcId>(e.id).id]);
    } else if (std::holds_alternative<DstId>(e.id)) {
        e.f(dst_nodes[std::get<DstId>(e.id).id]);
    } else if (std::holds_alternative<RtrId>(e.id)){
        e.f(routers[std::get<RtrId>(e.id).id]);
    } else {
        assert(false);
    }
}
