#ifndef ROUTER_H
#define ROUTER_H

#include "event.h"
#include <deque>
#include <iostream>

class Flit {
public:
    Flit(int n) : flit_num(n) {}
    int flit_num;
};

class Router {
public:
    Router(EventQueue &eq, int id, int radix);

    // Router::tick_event captures pointer to 'this', and is initialized in the
    // Router's constructor. Therefore, we should disallow moving/copying of
    // Router to prevent the mutation of 'this'.
    Router(const Router &) = delete;
    Router(Router &&) = default;

    void tick();
    void put(int port, const Flit &flit);
    void route_compute(int port);
    void vc_alloc(int port);
    void switch_alloc(int port);
    void switch_traverse(int port);

    int get_radix() const { return input_units.size(); }

    struct InputUnit {
        struct State {
            int global;
            int route;
            int output_vc;
            int pointer;
            int credit_count;
        } state;
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
    EventQueue &eventq;
    const Event tick_event;

public:
    int id; // numerical router ID
    std::vector<InputUnit> input_units;
    std::vector<OutputUnit> output_units;
};

#endif
