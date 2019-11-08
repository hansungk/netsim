#ifndef EVENT_H
#define EVENT_H

#include <functional>
#include <queue>
#include <iostream>

#define IDSTRLEN 20

enum IdType {
  ID_SRC,
  ID_DST,
  ID_RTR,
};

struct Id {
  IdType type;
  int value;
  bool operator<(const Id &b) const
  {
    return (type < b.type) || (type == b.type && value < b.value);
  }
  bool operator==(const Id &b) const
  {
    return type == b.type && value == b.value;
  }
};

#define ID_HASH(id) (((id).type << (sizeof((id).value) * 8)) | (id).value)


inline bool is_src(Id id) { return id.type == ID_SRC; };
inline bool is_dst(Id id) { return id.type == ID_DST; };
inline bool is_rtr(Id id) { return id.type == ID_RTR; };

static inline Id src_id(int id) { return (Id){.type = ID_SRC, .value = id}; }
static inline Id dst_id(int id) { return (Id){.type = ID_DST, .value = id}; }
static inline Id rtr_id(int id) { return (Id){.type = ID_RTR, .value = id}; }

char *id_str(Id id, char *s);

std::ostream &operator<<(std::ostream &out, const Id &id);

struct Router;

class Event {
public:
    Event(Id i, std::function<void(Router &)> f_) : id(i), f(f_) {}

    Id id;                           // target router ID
    std::function<void(Router &)> f; // callback on a specific router
};

class EventQueue {
public:
    EventQueue() = default;
    EventQueue(const EventQueue &) = delete;

    void schedule(long time, const Event &e);
    void reschedule(long reltime, const Event &e);

    bool empty() const { return queue.empty(); }
    size_t size() const { return queue.size(); }
    const Event &peek() const;
    Event pop();

    long curr_time() const { return time_; }
    void print_and_exit();

private:
    using TimeEventPair = std::pair<long, Event>;
    static constexpr auto cmp = [](const auto &p1, const auto &p2) {
        return p1.first > p2.first;
    };

    long time_{0};
    std::priority_queue<TimeEventPair, std::vector<TimeEventPair>,
                        decltype(cmp)>
        queue{cmp};
};

template <typename T> class Reg {
public:
    Reg(EventQueue &eq) : eventq{eq} {}

    // Get the value stored in this Reg.
    // T is likely to be an integer, so just return by value.
    T get() const {
        return val;
    }

    // Set the value stored in this Reg.
    // Triggers an event for each of all callbacks linked with this Reg.
    void set(T v) {
        val = v;
        for (const auto &cb : callbacks) {
            eventq.reschedule(1, Event{cb});
        }
    }

    void watch(std::function<void()> f) {
        callbacks.push_back(f);
    }

private:
    EventQueue &eventq;
    std::vector<std::function<void()>> callbacks{};
    T val{};
};

// A data structure that must be sent to the worker module when a master module
// makes a request.
//
// First, the worker module must be able to write the result to somewhere, and
// it knows that by the 'val' reference. (TODO: explain why this can't be value
// type)
//
// Second, since this is an event-driven simulator, the worker should be able
// to notify the master after it finishes processing the request by invoking a
// function.  To let the worker know what function to invoke, the master
// registers (preferably) one of its member function to this Req, using 'hook'.
template <typename T> class Req {
public:
    Req(T &v, std::function<void()> h) : val{v}, hook{h} {}

    // Write the result value, and call the event hook function.  This hook
    // will effectively notify the master module by initiating the finalizing
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
