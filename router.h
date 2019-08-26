#ifndef ROUTER_H
#define ROUTER_H

#include "event.h"
#include <deque>

class Flit {
public:
    Flit(int n) : flit_num(n) {}
    int flit_num;
};

class Router {
public:
    Router(EventQueue &eq_, int radix);

    void run();
    void route_compute(int port);
    void vc_alloc(int port);
    void switch_alloc(int port);
    void switch_traverse(int port);

    struct InputUnit {
        InputUnit(Router &r, const Event &e)
            : router(r), drain_event(e) {}
        void put(const Flit &flit);

        Router &router;
        Event drain_event;
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

    std::vector<InputUnit> input_units;
    std::vector<OutputUnit> output_units;

private:
    EventQueue &eventq;
};

#endif
