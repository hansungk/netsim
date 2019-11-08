#include "event.h"
#include <cassert>
#include <iostream>

char *id_str(Id id, char *s)
{
  int w;
  if (is_src(id))
    w = snprintf(s, IDSTRLEN, "Src ");
  else if (is_dst(id))
    w = snprintf(s, IDSTRLEN, "Dst ");
  else
    w = snprintf(s, IDSTRLEN, "Rtr ");
  snprintf(s + w, IDSTRLEN - w, "%d", id.value);
  return s;
}

std::ostream &operator<<(std::ostream &out, const Id &id) {
    if (is_src(id)) {
        out << "Src ";
    } else if (is_dst(id)) {
        out << "Dst ";
    } else {
        out << "Rtr ";
    }
    out << id.value;
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
