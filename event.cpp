#include "event.h"
#include <stdio.h>
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

void EventQueue::schedule(long time, const Event &e) {
    TimeEventPair p{time, e};
    queue.push(p);
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

long EventQueue::next_time() const
{
    return queue.top().first;
}
