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
    void run();
    void put(const Flit &flit);
    void vc_alloc();
    void switch_alloc();

    EventQueue eventq{};
    std::deque<Flit> in_buf{};
    std::deque<Flit> out_buf{};
};

#endif
