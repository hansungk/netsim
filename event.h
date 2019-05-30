// -*- C++ -*-
#ifndef EVENT_H
#define EVENT_H

#include <functional>
#include <queue>

// An Event has a callback function.
class Event {
public:
    Event(long time_, auto func_) : time(time_), func(func_) {}
    long time;                  // start time
    std::function<void()> func; // callback function
};

class EventQueue {
public:
    void schedule(const Event &e);
    bool empty() { return queue.empty(); }
    const Event &peek() const;
    Event pop();

private:
    std::queue<Event> queue;
};

#endif
