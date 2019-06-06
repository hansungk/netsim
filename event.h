// -*- C++ -*-
#ifndef EVENT_H
#define EVENT_H

#include <functional>
#include <queue>
#include <iostream> // XXX

// An Event has a callback function.
// This type is meant to be used by value.
class Event {
public:
    Event(std::function<void()> f) : func(f) {}

    std::function<void()> func; // event handler
};

class EventQueue {
public:
    EventQueue() = default;
    EventQueue(const EventQueue &) = delete;

    void schedule(long time, const Event &e);
    void reschedule(long time, const Event &e);
    bool empty() { return queue.empty(); }
    const Event &peek() const;
    Event pop();
    long time() const { return time_; }
    void print() const;

    // TODO: we need to keep a list of 'static' events that gets called for
    // _every_ event cycle, e.g. the polling operations.  Otherwise, the
    // simulator cannot autonomously add those events to the queue every time a
    // new event is generated.
private:
    struct TimedEvent {
        long time;
        Event event;
    };

    long time_{0};
    std::queue<TimedEvent> queue;
};

// A data structure that must be sent to the worker module when a master module
// makes a request.
//
// First, the worker module must be able to write the result to somewhere, so
// this should have a value field. (TODO: should this be a value, or a pointer?)
//
// Second, since this is an event-driven simulator, the worker should be able to
// notify the master after it finishes processing the request by invoking a
// function.  To let the worker know what function to invoke, the master
// registers (preferably) one of its member function to this Req, using 'hook'.
template <typename T> class Req {
public:
    Req(T &v, std::function<void()> h) : val(v), hook(h) {}

    // Write the result value, and call the event hook function.  This hook will
    // effectively 'notify' the master module by initiating the finalizing
    // operations on the master-side, e.g. setting a ready bit.
    void reply(const T &val_) {
        val = val_;
        hook();
    }

    T &val;                     // value field to be written by the receiver
    std::function<void()> hook; // function to be called after this request
                                // is replied
};

#endif
