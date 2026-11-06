#ifndef GRAPHITE_CONTAINERS_H
#define GRAPHITE_CONTAINERS_H

#include "internal.h"

/* Keep the container dependency private and apply the same OOM policy. */
#define uthash_malloc(size) gr_alloc(1, (size))
#define uthash_free(ptr, size) free(ptr)
#define uthash_fatal(message) abort()
#include "../../vendor/uthash.h"

#endif
