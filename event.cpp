#include "event.h"
#include <cassert>
#include <iostream>

std::ostream &operator<<(std::ostream &out, const Id &id) {
    int i;
    if (std::holds_alternative<SrcId>(id)) {
        out << "Src ";
        i = std::get<SrcId>(id).id;
    } else if (std::holds_alternative<DstId>(id)) {
        out << "Dst ";
        i = std::get<DstId>(id).id;
    } else {
        out << "Rtr ";
        i = std::get<RtrId>(id).id;
    }
    out << i;
    return out;
}

void EventQueue::schedule(long time, const Event &e) {
    TimeEventPair p{time, e};
    queue.push(p);
    // std::cout << "scheduled event at " << p.first << std::endl;
}

void EventQueue::reschedule(long reltime, const Event &e) {
    schedule(time_ + reltime, e);
}

const Event &EventQueue::peek() const {
    return queue.top().second;
}

Event EventQueue::pop() {
    assert(queue.top().first >= time_ && "time goes backward!");
    // Update simulation time.
    time_ = queue.top().first;
    Event e{peek()};
    queue.pop();
    return e;
}

void EventQueue::print_and_exit() {
    std::cout << "Event queue entries:\n";
    std::cout << "size=" << queue.size() << std::endl;
    while (!queue.empty()) {
        auto tp = queue.top();
        std::cout << "[@" << tp.first << ", " << tp.second.id << "]"
                  << std::endl;
        queue.pop();
    }
    exit(EXIT_SUCCESS);
}
