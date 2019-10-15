#ifndef ROUTER_H
#define ROUTER_H

#include "event.h"
#include <deque>
#include <iostream>
#include <map>
#include <optional>

using RouterPortPair = std::pair<Id, int /*port*/>;

// Encodes channel connectivity in a bidirectional map.
// Supports runtime checking for connectivity error.
class Topology {
public:
    static constexpr RouterPortPair not_connected{RtrId{-1}, -1};

    static Topology ring(int n);

    Topology() = default;
    Topology(std::initializer_list<std::pair<RouterPortPair, RouterPortPair>>);

    RouterPortPair find_forward(RouterPortPair out_port) {
        auto it = forward_map.find(out_port);
        if (it == forward_map.end()) {
            return not_connected;
        } else {
            return it->second;
        }
    }

    RouterPortPair find_reverse(RouterPortPair in_port) {
        auto it = reverse_map.find(in_port);
        if (it == reverse_map.end()) {
            return not_connected;
        } else {
            return it->second;
        }
    }

    bool connect(const RouterPortPair src, const RouterPortPair dst);

    auto &get_forward_map() { return forward_map; }

private:
    std::map<RouterPortPair /* upstream output port */,
             RouterPortPair /* downstream input port */>
        forward_map;
    std::map<RouterPortPair /* downstream input port */,
             RouterPortPair /* upstream ouptut port */>
        reverse_map;
};

/// Flit and credit encoding.
/// Follows Fig. 16.13.
class Flit {
public:
    enum class Type {
        Head,
        Body,
        Tail,
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

std::ostream &operator<<(std::ostream &out, const Flit &flit);

class Credit {
public:
    // VC is omitted, as we only have one VC per a physical channel.
};

class Channel {
public:
    Channel(EventQueue &eq, Id id_, const long dl, const RouterPortPair s,
            const RouterPortPair d);

    void put(const Flit &flit);
    void put_credit(const Credit &credit);
    std::optional<Flit> get();
    std::optional<Credit> get_credit();

    RouterPortPair src;
    RouterPortPair dst;

    Id id;

private:
    EventQueue &eventq;
    const long delay;
    const Event tick_event; // self-tick event.
    std::deque<std::pair<long, Flit>> buf;
    std::deque<std::pair<long, Credit>> buf_credit;
};

using ChannelRefVec = std::vector<std::reference_wrapper<Channel>>;
// Custom printer for Flit
std::ostream &operator<<(std::ostream &out, const Flit &flit);

/// A node.  Despite its name, it can represent any of a router node, a source
/// node and a destination node.
class Router {
public:
    Router(EventQueue &eq, Id id, int radix,
           const ChannelRefVec &in_chs,
           const ChannelRefVec &out_chs);
    // Router::tick_event captures pointer to 'this' in the Router's
    // constructor. To prevent invalidating the 'this' pointer, we should
    // disallow moving/copying of Router.
    Router(const Router &) = delete;
    Router(Router &&) = default;

    // Tick event
    void tick();

    // Pipeline stages
    enum class PipelineStage {
        Idle,
        RC,
        VA,
        SA,
        ST,
    };

    void source_generate();
    void destination_consume();
    void fetch_flit();
    void fetch_credit();
    void credit_update();
    void route_compute();
    void vc_alloc();
    void switch_alloc();
    void switch_traverse();

    // Allocators and arbiters
    int vc_arbit_round_robin(int out_port);

    // Misc
    const Event &get_tick_event() const { return tick_event; }
    int get_radix() const { return input_units.size(); }

public:
    struct InputUnit {
        struct State {
            enum class GlobalState {
                Idle,
                Routing,
                VCWait,
                Active,
                CreditWait,
            } global{GlobalState::Idle};
            int route_port{-1};
            int output_vc{0};
            int pointer;
            // credit count is omitted; it can be found in the output
            // units instead.
        } state;
        PipelineStage stage{PipelineStage::Idle};
        std::deque<Flit> buf;
    };

    struct OutputUnit {
        OutputUnit(long bufsize) { state.credit_count = bufsize; }
        struct State {
            enum class GlobalState {
                Idle,
                Active,
                CreditWait,
            } global{GlobalState::Idle};
            int input_port{-1};
            int input_vc{0};
            int credit_count; // FIXME: hardcoded
        } state;
        // std::deque<Flit> buf;
        std::optional<Credit> buf_credit;
    };

    Id id; // numerical router ID

private:
    // Debug output stream
    std::ostream &dbg() const;

    // Mark self-reschedule on the next tick
    void mark_self_reschedule() { reschedule_next_tick = true; }

private:
    EventQueue &eventq;     // reference to the simulator-global event queue
    const Event tick_event; // self-tick event.
    const size_t input_buf_size{6};
    long last_tick{-1}; // record the last tick time to prevent double-tick in
                        // single cycle
    long last_reschedule_tick{-1}; // XXX: hacky?
    long flit_payload_counter{0};  // for simple payload generation
    bool reschedule_next_tick{
        false}; // marks whether to self-tick at the next cycle
    // const std::vector<RouterPortPair>
    //     input_origins; // stores the other end of the input ports. Used for the
    //                    // returning of credits.
    // const std::vector<RouterPortPair>
    //     output_destinations; // stores the other end of the output ports

    const ChannelRefVec
        input_channels; // references to the input channels for each port
    const ChannelRefVec
        output_channels; // references to the input channels for each port

    std::vector<InputUnit> input_units;
    std::vector<OutputUnit> output_units;

    // Allocator
    int last_grant_input{0}; // Round-robin allocator
};

#endif
