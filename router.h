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
    using RouterPortPair = std::pair<int /*router*/, int /*port*/>;

    RouterPortPair find(RouterPortPair input) {
        auto it = in_out_map.find(input);
        if (it == in_out_map.end()) {
            return not_connected;
        } else {
            return it->second;
        }
    }

    bool connect(const RouterPortPair input, const RouterPortPair output);

    static constexpr RouterPortPair not_connected{-1, -1};

private:
    std::map<RouterPortPair, RouterPortPair> in_out_map;
    std::map<RouterPortPair, RouterPortPair> out_in_map;
};

class Flit {
public:
    Flit(int n) : flit_num(n) {}
    int flit_num;
};

class Router {
public:
    Router(EventQueue &eq, int id, int radix,
           const std::vector<Topology::RouterPortPair> &dp);

    // Router::tick_event captures pointer to 'this', and is initialized in the
    // Router's constructor. Therefore, we should disallow moving/copying of
    // Router to prevent the mutation of 'this'.
    Router(const Router &) = delete;
    Router(Router &&) = default;

    void tick();
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
    EventQueue &eventq;     // reference to the simulator-global event queue
    const Event tick_event; // self-tick event.
    long last_tick; // record the last tick time to prevent double-tick in
                    // single cycle
    bool reschedule_next_tick{false}; // self-tick at next cycle?
    std::vector<Topology::RouterPortPair>
        destination_ports; // where are my output ports connected to?

public:
    int id; // numerical router ID
    std::vector<InputUnit> input_units;
    std::vector<OutputUnit> output_units;
};

#endif
