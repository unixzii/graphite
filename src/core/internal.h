#ifndef GRAPHITE_INTERNAL_H
#define GRAPHITE_INTERNAL_H

#include <graphite/backend.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* Internal allocation failures and representational overflows are fatal. */
void *gr_alloc(size_t count, size_t size);
void *gr_copy(const void *data, size_t count, size_t size);
void gr_reserve(void **data, size_t *capacity, size_t count, size_t size);
uint64_t gr_next_id(void);

static inline void gr_shared_ref(atomic_size_t *refs)
{
    size_t old = atomic_fetch_add_explicit(refs, 1, memory_order_relaxed);
    if (old == 0 || old == SIZE_MAX)
        abort();
}

static inline bool gr_shared_unref(atomic_size_t *refs)
{
    size_t old = atomic_fetch_sub_explicit(refs, 1, memory_order_acq_rel);
    if (old == 0)
        abort();
    return old == 1;
}

static inline void gr_local_ref(size_t *refs)
{
    if (*refs == 0 || *refs == SIZE_MAX)
        abort();
    ++*refs;
}

static inline bool gr_local_unref(size_t *refs)
{
    if (*refs == 0)
        abort();
    return --*refs == 0;
}

static inline bool gr_valid_point(gr_point p)
{
    return isfinite(p.x) && isfinite(p.y);
}

static inline bool gr_valid_rect(gr_rect r)
{
    return gr_valid_point(r.origin) && isfinite(r.size.width) && isfinite(r.size.height) && r.size.width >= 0 &&
           r.size.height >= 0 && isfinite(r.origin.x + r.size.width) && isfinite(r.origin.y + r.size.height);
}

static inline bool gr_valid_color(gr_color c)
{
    return isfinite(c.r) && c.r >= 0 && c.r <= 1 && isfinite(c.g) && c.g >= 0 && c.g <= 1 && isfinite(c.b) &&
           c.b >= 0 && c.b <= 1 && isfinite(c.a) && c.a >= 0 && c.a <= 1;
}

static inline bool gr_valid_rule(gr_fill_rule r)
{
    return r == GR_FILL_NONZERO || r == GR_FILL_EVEN_ODD;
}

#endif
