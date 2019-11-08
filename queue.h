#ifndef QUEUE_H
#define QUEUE_H

#include <stdlib.h>

void *queue_initf(void *a, size_t elemsize, size_t len);
#ifdef __cplusplus
template <class T> static T *queue_init_wrapper(T *a, size_t elemsize, size_t len) {
  return (T*)queue_initf(a, elemsize, len);
}
#else
#define queue_init_wrapper queue_initf
#endif

#define queue_init(a, n) ((a) = queue_init_wrapper((a), sizeof(*(a)), (n)))
#define queue_full(a) (queue_len((a)) == (long)queue_header(a)->cap - 1)
#define queue_empty(a) (queue_header(a)->front == queue_header(a)->back)
#define queue_put(a, elem)                                                     \
  (queue_full(a) ? NULL                                                        \
                 : (a[queue_header(a)->back] = (elem),                         \
                    queue_header(a)->back =                                    \
                        (queue_header(a)->back + 1) % queue_header(a)->cap,    \
                    (a)))
#define queue_front(a) (a[queue_header(a)->front])
#define queue_fronti(a) (queue_header(a)->front)
#define queue_backi(a) (queue_header(a)->back)
#define queue_cap(a) (queue_header(a)->cap)
#define queue_pop queue_popf

#define queue_header(t) ((Queue *)(t) - 1)

struct Queue {
  size_t cap;
  long front;
  long back;
};

void queue_free(void *a);
long queue_len(const void *a);
void queue_popf(void *a);

#endif
