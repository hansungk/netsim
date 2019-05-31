// -*- C++ -*-
#ifndef EVENT_H
#define EVENT_H

#include <functional>
#include <queue>

// An Event has a callback function.
class Event {
public:
    Event(long time_, std::function<void()> f) : time(time_), func(f) {}
    long time;                  // start time
    std::function<void()> func; // event handler
};

class EventQueue {
public:
    EventQueue() = default;
    EventQueue(const EventQueue &) = delete;

    void schedule(const Event &e);
    bool empty() { return queue.empty(); }
    const Event &peek() const;
    Event pop();

    // TODO: we need to keep a list of 'static' events that gets called for
    // _every_ event cycle, e.g. the polling operations.  Otherwise, the
    // simulator cannot autonomously add those events to the queue every time a
    // new event is generated.
private:
    std::queue<Event> queue;
};

// A data structure that must be sent to the worker module when a master module
// makes a request.
//
// First, the worker module must be able to write the result to somewhere, so
// this should have a value field. (TODO: should this be a value, or a pointer?)
//
// Second, since this is an event-driven simulator, the worker should be able to
// invoke a notifier function after the request finishes.  That's what 'hook' is
// for.
template <typename T> class Req {
public:
    Req(std::function<void()> h) : hook(h) {}

    // Write the result value, and call the event hook function.  This hook will
    // effectively 'notify' the master module by initiating the finalizing
    // operations on the master-side, e.g. setting a ready bit.
    void reply(const T &val_) {
        val = std::move(val_);
        hook();
    }

    T val;                      // value field to be written by the receiver
    std::function<void()> hook; // function to be called after this request
                                // is replied
};

#endif
