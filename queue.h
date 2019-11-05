#ifndef QUEUE_H
#define QUEUE_H

#include <stdlib.h>

struct Queue {
  void **buf;
  size_t cap;
  long front;
  long back;
};

Queue queue_create(size_t size);
void queue_destroy(Queue *q);
long queue_len(const Queue *q);
void *queue_put(Queue *q, void *elem);
void *queue_pop(Queue *q);

#endif
