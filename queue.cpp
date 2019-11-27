#include "queue.h"
#include <assert.h>

// Initialize a circular queue with specified size.
void *queue_initf(void *a, size_t elemsize, size_t len)
{
    a = NULL; // Squelch error.
    void *b = malloc((len + 1) * elemsize + sizeof(Queue));
    b = (char *)b + sizeof(Queue);
    queue_header(b)->cap = len + 1;
    queue_header(b)->front = 0;
    queue_header(b)->back = 0;
    return b;
}

void queue_free(void *a) { free(queue_header(a)); }

long queue_len(const void *a)
{
    Queue *q = queue_header(a);
    return (q->back + q->cap - q->front) % q->cap;
}

// Pop an element from the front of the queue.
void queue_popf(void *a)
{
    Queue *q = queue_header(a);
    if (!queue_empty(a))
        q->front = (q->front + 1) % q->cap;
}
