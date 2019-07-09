#include "event.h"
#include <cassert>
#include <iostream>

void EventQueue::schedule(long time, const Event &e) {
    TimeEventPair p{time, e};
    queue.push(p);
    // std::cout << "scheduled event at " << p.first << std::endl;
}

void EventQueue::reschedule(long time, const Event &e) {
    TimeEventPair p{time_ + time, e};
    queue.push(p);
    // std::cout << "scheduled event at " << p.first << std::endl;
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

void EventQueue::print() const {
    std::cout << "Event queue entries:\n";
    std::cout << "size=" << queue.size() << std::endl;
}
