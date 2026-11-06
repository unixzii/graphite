#include "internal.h"

void *gr_alloc(size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size) abort();
    size_t bytes = count * size;
    if (!bytes) return NULL;
    void *result = malloc(bytes);
    if (!result) abort();
    return result;
}

void *gr_copy(const void *data, size_t count, size_t size)
{
    void *result = gr_alloc(count, size);
    if (count && size) memcpy(result, data, count * size);
    return result;
}

void gr_reserve(void **data, size_t *capacity, size_t count, size_t size)
{
    if (count <= *capacity) return;
    if (!size || count > SIZE_MAX / size) abort();
    size_t next = *capacity ? *capacity : 8;
    while (next < count) {
        if (next > SIZE_MAX / 2) { next = count; break; }
        next *= 2;
    }
    if (next > SIZE_MAX / size) next = count;
    void *result = realloc(*data, next * size);
    if (!result) abort();
    *data = result;
    *capacity = next;
}

uint64_t gr_next_id(void)
{
    static atomic_uint_fast64_t next = 1;
    uint_fast64_t id = atomic_fetch_add_explicit(&next, 1, memory_order_relaxed);
    if (id == 0 || id >= UINT64_MAX) abort();
    return (uint64_t)id;
}
