#include "router.h"
#include <iostream>

void Router::put(const Flit &flit) {
    // If the buffer was empty, kickstart the pipeline
    if (in_buf.empty())
        eventq.reschedule(1, Event{[this] { vc_alloc(); }});

    in_buf.push_back(flit);
}

void Router::vc_alloc() {
    std::cout << "[" << in_buf.front().flit_num << "] vc allocation\n";

    eventq.reschedule(1, Event{[this] { switch_alloc(); }});
}

void Router::switch_alloc() {
    std::cout << "[" << in_buf.front().flit_num << "] switch allocation\n";

    Flit flit = in_buf.front();
    in_buf.pop_front();
    out_buf.push_back(flit);

    if (!in_buf.empty()) {
        eventq.reschedule(1, Event{[this] { vc_alloc(); }});
    }
}

void Router::run() {
    while (!eventq.empty()) {
        auto e = eventq.pop();
        std::cout << "[event @ t=" << eventq.time() << ":]\n";
        e.func();
    }
}
