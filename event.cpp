#include "event.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

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

static int cmp_pri(pqueue_pri_t next, pqueue_pri_t curr) { return next > curr; }
static pqueue_pri_t get_pri(void *a) { return ((TimedEvent *)a)->time; }
static void set_pri(void *a, pqueue_pri_t pri) { ((TimedEvent *)a)->time = pri; }
static size_t get_pos(void *a) { return ((TimedEvent *)a)->pos; }
static void set_pos(void *a, size_t pos) { ((TimedEvent *)a)->pos = pos; }

void eventq_init(EventQueue *eq)
{
    eq->time_ = -1;
    eq->pq = pqueue_init(1000, cmp_pri, get_pri, set_pri, get_pos, set_pos);
    assert(eq->pq);
}

void eventq_destroy(EventQueue *eq)
{
    while (pqueue_size(eq->pq) > 0) {
        TimedEvent *te = (TimedEvent *)pqueue_pop(eq->pq);
        free(te);
    }
    pqueue_free(eq->pq);
}

void schedule(EventQueue *eq, long time, Event e)
{
    TimedEvent *te = (TimedEvent *)malloc(sizeof(TimedEvent));
    te->time = time;
    te->event = e;
    pqueue_insert(eq->pq, te);
}

void reschedule(EventQueue *eq, long reltime, Event e)
{
    schedule(eq, eq->time_ + reltime, e);
}

long curr_time(const EventQueue *eq)
{
    return eq->time_;
}

// This is mainly used for the debugger, where it should be able to process
// all events at a specific time and stop right before the time changes.
long next_time(const EventQueue *eq)
{
    return ((TimedEvent *)pqueue_peek(eq->pq))->time;
}

Event eventq_pop(EventQueue *eq)
{
    TimedEvent *te = (TimedEvent *)pqueue_pop(eq->pq);
    long time = te->time;
    Event e = te->event;
    assert(time >= eq->time_ && "time goes backward!");
    // Update simulation time.
    eq->time_ = time;
    free(te);
    return e;
}

int eventq_empty(const EventQueue *eq)
{
    return pqueue_size(eq->pq) == 0;
}
