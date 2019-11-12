#include "mem.h"
#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Debug mode. */
#define DEBUG 0

#define ARENA_INIT_SIZE 4096

/* Size of the tag for each element.
 * The higher this is, the more times each slot can be reallocated before it
 * gets disabled, and the slower the total amount of allocated memory increases.
 * The memory overhead increases as well, however. */
#define TAGSIZE 2
#define INDEXSIZE (sizeof(Handle) - TAGSIZE)
#define ALIGNMENT 8

/* Integer type used for the bitset. */
typedef uintmax_t intset_t;

/* The allocator. */
typedef struct Alloc {
  uint8_t *arena;     /* Preallocated memory arena. */
  size_t size;        /* Total size of the arena. */
  long nslots;        /* Total number of slots. */
  long nalloc;        /* Number of allocated objects. */
  size_t elemsize;    /* Byte size of each object. */
  intset_t *freelist; /* Bitset of free slots; 0:occupied, 1:free. */
  long firstfree;     /* Leftmost last-known-to-be-free slot. */
} Alloc;

/* Math macros. */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define ALIGNTO(n, sz) (((n) + (sz) - 1) / (sz) * (sz))
#define SLOTSIZE(n) (ALIGNTO(n + TAGSIZE, ALIGNMENT))
#define NSLOTS(a) ((a)->size / SLOTSIZE(a->elemsize))

/* Bitset macros. */
#define bitsizeof(n) (sizeof(n) * 8)
#define bitset_index(n) ((n) / bitsizeof(intset_t))
#define bitset_bitpos(n) ((n) % bitsizeof(intset_t))
#define bitset_len(n) (ALIGNTO(n, bitsizeof(intset_t)) / bitsizeof(intset_t))
#define onehot(pos) (~((~0ul) >> 1) >> bitset_bitpos(pos))

/* Grow the arena and freelist to a heuristic size that is enough to contain the
 * specified number of additional objects. */
static Alloc *alloc_grow(Alloc *a, size_t addsize)
{
  size_t newsize = a->size + addsize;
  if (newsize < ARENA_INIT_SIZE)
    newsize = ARENA_INIT_SIZE;
  else if (newsize < 2 * a->size)
    newsize = 2 * a->size;
  uint8_t *p = (uint8_t *)realloc(a->arena, newsize);
  if (!p)
    return NULL;
  memset(p + a->size, 0, newsize - a->size);
  a->arena = p;
  a->size = newsize;
  a->nslots = NSLOTS(a);
  ptrdiff_t len = arrlen(a->freelist);
  arrsetlen(a->freelist, bitset_len(NSLOTS(a)));
  ptrdiff_t diff = arrlen(a->freelist) - len;
  memset(a->freelist + len, ~0, sizeof(a->freelist[0]) * diff);
  return a;
}

Alloc *alloc_create(size_t elemsize)
{
    Alloc *a = (Alloc *)malloc(sizeof(Alloc));
  *a = (Alloc){.elemsize = elemsize};
  return alloc_grow(a, 1);
}

void alloc_destroy(Alloc *a)
{
  if (a->arena) free(a->arena);
  if (a->freelist) arrfree(a->freelist);
  free(a);
}

static long find_first_free(Alloc *a)
{
  ptrdiff_t i = bitset_index(a->firstfree);
  long count = 0;
  for (; i < arrlen(a->freelist); i++, count++)
    if (a->freelist[i] != 0) break;
  if (i == arrlen(a->freelist)) return -1;
  return (i * bitsizeof(intset_t)) + __builtin_clzl(a->freelist[i]);
}

static uintmax_t alloc_read_tag(Alloc *a, long slot)
{
  ptrdiff_t offset = slot * SLOTSIZE(a->elemsize);
  uint8_t *p = a->arena + offset + (SLOTSIZE(a->elemsize) - TAGSIZE);
  uintmax_t tag = 0;
  memcpy(&tag, p, TAGSIZE);
  return tag;
}

static void alloc_write_tag(Alloc *a, long slot, uintmax_t tag)
{
  ptrdiff_t offset = slot * SLOTSIZE(a->elemsize);
  uint8_t *p = a->arena + offset + (SLOTSIZE(a->elemsize) - TAGSIZE);
  memcpy(p, &tag, TAGSIZE);
}

/* A handle consists of the tag and the slot index. The slot index is used to
 * safely address the position of the object, without being affected by the
 * reallocation of the memory arena. The tag is used for the use-after-free
 * check, and also for identifying the actual owner of the object.
 *
 * The MSB of the tag bits is the 'valid' bit, signifying whether the slot is
 * currently allocated or not. The rest of the tag bits are the 'unique ID'
 * bits, which is compared against the handle to check if the handle does point
 * to the same object that is currently stored in the slot, and not an older or
 * newer one that happens to reside in the same slot.
 *
 *                        +-+---------+------------+
 *               Handle:  |v| uniq ID | slot index |
 *                        +-+---------+------------+
 *                        <--TAGSIZE->|<-INDEXSIZE->
 *
 * As with the handle, a tag with the same structure is stored within each
 * slot. Since a legal access requires the slot to be both valid and have the
 * same ID, the only way that a successful match can happen for a handle is if
 * all of its tag bits completely match the tag stored in the slot.
 *
 * Current implementation of the unique ID is a simple generation counter that
 * gets bumped every time the slot is freed.
 */

#define TAGMSB (1ul << (TAGSIZE * 8 - 1))
#define TAGMASK ((~0ul) << (INDEXSIZE * 8))
#define tag_valid(t) ((TAGMSB & (t)) != 0)
#define tag_id(t) ((~TAGMSB) & (t))
#define id_trim(id) ((id) & ~TAGMSB)
#define tag_create(v, id) ((v) ? (TAGMSB | id_trim(id)) : id_trim(id))
#define handle_tag(h) (((h) & TAGMASK) >> (INDEXSIZE * 8))
#define handle_slot(h) ((h) & ~TAGMASK)
#define handle_create(s, id) ((s) | (tag_create(1, id) << (INDEXSIZE * 8)))

Handle zalloc(Alloc *a)
{
  if (a->nalloc >= a->nslots)
    if (!alloc_grow(a, 1)) return 0;
  long slot = find_first_free(a);
  a->firstfree = slot;
  a->freelist[bitset_index(slot)] &= ~onehot(slot);
  a->nalloc++;
  uintmax_t id = tag_id(alloc_read_tag(a, slot));
  uintmax_t tag = tag_create(1, id);
  alloc_write_tag(a, slot, tag);
  return handle_create(slot, tag);
}

/* Check if the object pointed by the given handle is still alive in the
 * allocator. */
static int zcheck(Alloc *a, Handle h)
{
  uintmax_t stag = alloc_read_tag(a, handle_slot(h));
  uintmax_t htag = handle_tag(h);
  if (DEBUG && stag != htag) {
    if (!tag_valid(stag))
      fprintf(stderr, "USE-AFTER-FREE!\n");
    else
      fprintf(stderr, "ID MISMATCH!\n");
  }
  return stag == htag;
}

void *zptr(Alloc *a, Handle h)
{
  if (!zcheck(a, h)) return NULL;
  long slot = handle_slot(h);
  return a->arena + SLOTSIZE(a->elemsize) * slot;
}

void zfree(Alloc *a, Handle h)
{
  if (!zcheck(a, h)) return;
  long slot = handle_slot(h);
  a->freelist[bitset_index(slot)] |= onehot(slot);
  a->firstfree = MIN(a->firstfree, slot);
  a->nalloc--;
  uintmax_t id = tag_id(handle_tag(h));
  alloc_write_tag(a, slot, tag_create(0, id + 1));
}
