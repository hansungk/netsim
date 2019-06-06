#include "event.h"
#include <cassert>

void EventQueue::schedule(long time, const Event &e) {
    TimedEvent te{time, e};
    queue.push(te);
    std::cout << "scheduled event at " << te.time << std::endl;
}

void EventQueue::reschedule(long time, const Event &e) {
    TimedEvent te{time_ + time, e};
    queue.push(te);
    std::cout << "scheduled event at " << te.time << std::endl;
}

const Event &EventQueue::peek() const {
    return queue.front().event;
}

Event EventQueue::pop() {
    // Update simulation time.
    time_ = queue.front().time;
    Event e{peek()};
    queue.pop();
    return e;
}

void EventQueue::print() const {
    std::cout << "Event queue entries:\n";
    std::cout << "size=" << queue.size() << std::endl;
}
