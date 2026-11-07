#include <malloc/malloc.h>
#include <stdlib.h>

size_t bench_live, bench_count;

void *bench_malloc(size_t size)
{
    void *p = malloc(size);
    if (p)
    {
        bench_live += malloc_size(p);
        ++bench_count;
    }
    return p;
}

void *bench_realloc(void *p, size_t size)
{
    size_t old = p ? malloc_size(p) : 0;
    void *q = realloc(p, size);
    if (q)
    {
        bench_live = bench_live - old + malloc_size(q);
        ++bench_count;
    }
    return q;
}

void bench_free(void *p)
{
    if (p)
        bench_live -= malloc_size(p);
    free(p);
}
