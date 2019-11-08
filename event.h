#ifndef EVENT_H
#define EVENT_H

#include <queue>

#define IDSTRLEN 20

enum IdType {
    ID_SRC,
    ID_DST,
    ID_RTR,
};

struct Id {
    IdType type;
    int value;
};

inline bool is_src(Id id) { return id.type == ID_SRC; };
inline bool is_dst(Id id) { return id.type == ID_DST; };
inline bool is_rtr(Id id) { return id.type == ID_RTR; };

static inline Id src_id(int id) { return (Id){.type = ID_SRC, .value = id}; }
static inline Id dst_id(int id) { return (Id){.type = ID_DST, .value = id}; }
static inline Id rtr_id(int id) { return (Id){.type = ID_RTR, .value = id}; }

char *id_str(Id id, char *s);

struct Router;

void tick_func(Router *);

class Event {
public:
    Event(Id i, void (*f_)(Router *)) : id(i), f(f_) {}

    Id id;                           // target router ID
    void (*f)(Router *);
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
    // This is mainly used for the debugger, where it should be able to process
    // all events at a specific time and stop right before the time changes.
    long next_time() const;

private:
    using TimeEventPair = std::pair<long, Event>;
    static constexpr auto cmp = [](const auto &p1, const auto &p2) {
        return p1.first > p2.first;
    };

    long time_{-1};
    std::priority_queue<TimeEventPair, std::vector<TimeEventPair>,
                        decltype(cmp)>
        queue{cmp};
};

#endif
