#ifndef MEM_H
#define MEM_H

#include <stddef.h>
#include <stdint.h>

/* Memory pool allocator.
 *
 * Caveats:
 *
 * 1. The allocator currently only supports little endian systems, and is
 *    targeted for 64-bit systems.
 * 2. Assumes that a byte is 8 bits large. */

/* Public opaque type for the allocator. */
typedef struct Alloc Alloc;

/* Handle to the allocated memory.
 * Change the integer type to a different width according to your
 * need. */
typedef uint64_t Handle;

Alloc *alloc_create(size_t itemsize);
void alloc_destroy(Alloc *a);
Handle zalloc(Alloc *a);
void *zptr(Alloc *a, Handle h);
void zfree(Alloc *a, Handle h);

#endif // MEM_H
