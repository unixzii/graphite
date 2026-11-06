#include <stdlib.h>
void *bench_malloc(size_t size);
void *bench_realloc(void *pointer, size_t size);
void bench_free(void *pointer);
#define malloc bench_malloc
#define realloc bench_realloc
#define free bench_free
