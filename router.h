#ifndef ROUTER_H
#define ROUTER_H

#include "event.h"
#include <deque>
#include <iostream>
#include <map>

// Encodes router topology in a bidirectional map.
// Supports runtime checking for connectivity error.
class Topology {
public:
    using RouterPortPair = std::pair<NodeId, int /*port*/>;
    static constexpr RouterPortPair not_connected{RtrId{-1}, -1};

    static Topology ring();

    Topology() = default;
    Topology(std::initializer_list<std::pair<RouterPortPair, RouterPortPair>>);

    RouterPortPair find(RouterPortPair input) {
        auto it = in_out_map.find(input);
        if (it == in_out_map.end()) {
            return not_connected;
        } else {
            return it->second;
        }
    }

    bool connect(const RouterPortPair input, const RouterPortPair output);

    // Helper functions to get ID of terminal nodes.
    static unsigned int src(unsigned int id) { return -id - 1; }
    static unsigned int dst(unsigned int id) { return -id - 1; }

private:
    std::map<RouterPortPair, RouterPortPair> in_out_map;
    std::map<RouterPortPair, RouterPortPair> out_in_map;
};

// Flit encoding.
// Follows Fig. 16.13.
class Flit {
public:
    enum class Type {
        Head,
        Body,
    };

    Flit(Type t, int src, int dst, long p) : type(t), payload(p) {
        route_info.src = src;
        route_info.dst = dst;
    }

    Type type;
    struct RouteInfo {
        int src;    // source node ID
        int dst{3}; // destination node ID
    } route_info;
    long payload;
};

/// A router (or a "switch") node.
class Router {
public:
    Router(EventQueue &eq, NodeId id, int radix,
           const std::vector<Topology::RouterPortPair> &dp);

    // Router::tick_event captures pointer to 'this', and is initialized in the
    // Router's constructor. Therefore, we should disallow moving/copying of
    // Router to prevent the mutation of 'this'.
    Router(const Router &) = delete;
    Router(Router &&) = default;

    void tick();
    const Event &get_tick_event() const { return tick_event; }
    void put(int port, const Flit &flit);
    void route_compute();
    void vc_alloc();
    void switch_alloc();
    void switch_traverse();

    int get_radix() const { return input_units.size(); }

    enum class PipelineStage {
        Idle,
        RC,
        VA,
        SA,
        ST,
    };

    struct InputUnit {
        struct State {
            enum class GlobalState {
                Idle,
                Routing,
                VCWait,
                Active,
                CreditWait,
            } global;
            int route;
            int output_vc;
            int pointer;
            int credit_count;
        } state;
        PipelineStage stage{PipelineStage::Idle};
        std::deque<Flit> buf;
    };

    struct OutputUnit {
        struct State {
            int global;
            int input_vc;
            int credit_count;
        } state;
        std::deque<Flit> buf;
    };

private:
    // Debug output stream
    std::ostream &dbg() const;

    // Mark self-reschedule on the next tick
    void mark_self_reschedule() { reschedule_next_tick = true; }

    EventQueue &eventq;     // reference to the simulator-global event queue
    const Event tick_event; // self-tick event.
    long last_tick{-1}; // record the last tick time to prevent double-tick in
                        // single cycle
    long last_reschedule_tick{-1};
    long flit_payload_counter{0};
    bool reschedule_next_tick{false}; // self-tick at next cycle?
    const std::vector<Topology::RouterPortPair>
        output_destinations; // stores the other end of the output ports

public:
    NodeId id; // numerical router ID
    std::vector<InputUnit> input_units;
    std::vector<OutputUnit> output_units;
};

#endif
