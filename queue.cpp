#include "queue.h"
#include <assert.h>

// Create a circular queue with specified size.
Queue queue_create(size_t n)
{
  void **buf = (void **)calloc(n + 1, sizeof(void *));
  assert(buf);
  return (Queue){
      .buf = buf,
      .cap = n + 1,
      .front = 0,
      .back = 0,
  };
}

void queue_destroy(Queue *q)
{
  free(q->buf);
}

long queue_len(const Queue *q)
{
  return (q->back + q->cap - q->front) % q->cap;
}

// Push an element in the back of the queue.
void *queue_put(Queue *q, void *elem)
{
  if (queue_len(q) == q->cap - 1)
    return NULL;
  void *p = q->buf[q->back] = elem;
  q->back = (q->back + 1) % q->cap;
  return p;
}

// Pop an element from the front of the queue, returning it.
void *queue_pop(Queue *q)
{
  if (q->front == q->back)
    return NULL;
  void *p = q->buf[q->front];
  q->front = (q->front + 1) % q->cap;
  return p;
}
