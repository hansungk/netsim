#ifndef EVENT_H
#define EVENT_H

#include "stdio.h"
extern "C" {
#include "pqueue.h"
}

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

struct Event {
    Id id;                           // target router ID
    void (*f)(Router *);
};

struct TimedEvent {
    long time;
    Event event;
    size_t pos; // used for libpqueue
};

struct EventQueue {
    long time_;
    pqueue_t *pq;
};

void eventq_init(EventQueue *eq);
void eventq_destroy(EventQueue *eq);
Event eventq_pop(EventQueue *eq);
int eventq_empty(const EventQueue *eq);
void schedule(EventQueue *eq, long time, Event e);
void reschedule(EventQueue *eq, long reltime, Event e);
long curr_time(const EventQueue *eq);
long next_time(const EventQueue *eq);

#endif
