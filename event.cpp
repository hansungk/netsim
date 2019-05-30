#include "event.h"

void EventQueue::schedule(const Event &e) {
    queue.push(e);
}

const Event &EventQueue::peek() const {
    return queue.back();
}

Event EventQueue::pop() {
    auto e = peek();
    queue.pop();
    return e;
}
