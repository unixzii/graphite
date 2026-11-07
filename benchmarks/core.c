#include <graphite/backend.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef METRICS
extern size_t bench_live, bench_count;
#else
static size_t bench_live, bench_count;
#endif
static void idle(void *p)
{
    (void)p;
}

static void apply(void *p, const gr_commit_batch *batch)
{
    (void)p;
    __asm__ volatile("" : : "r"(batch) : "memory");
}

static unsigned long long now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000000000 + t.tv_nsec;
}

int main(int argc, char **argv)
{
    if (argc != 4)
        return 1;
    size_t n = strtoull(argv[1], NULL, 10), changed = strtoull(argv[2], NULL, 10), rounds = strtoull(argv[3], NULL, 10);
    if (changed > n)
        return 1;
    size_t baseline = bench_live;
    unsigned long long start = now();
    gr_scene *scene = gr_scene_new((gr_size){1000, 1000});
    gr_backend_vtable vtable = {idle, apply, idle};
    if (gr_scene_set_backend(scene, &vtable, NULL) != GR_OK)
        return 2;
    gr_layer *root = gr_scene_get_root(scene);
    gr_layer **layers = malloc(n * sizeof(*layers));
    for (size_t i = 0; i < n; ++i)
    {
        layers[i] = gr_layer_new(scene, GR_LAYER_CONTAINER);
        if (gr_layer_add_child(root, layers[i]) != GR_OK)
            return 3;
    }
    gr_scene_commit(scene);
    unsigned long long create_ns = now() - start;
    for (size_t frame = 0; frame < 20; ++frame)
    {
        for (size_t i = 0; i < changed; ++i)
            gr_layer_set_opacity(layers[i], frame % 2 ? 1 : 0.5f);
        gr_scene_commit(scene);
    }
    size_t live = bench_live, count = bench_count;
    start = now();
    for (size_t frame = 0; frame < rounds; ++frame)
    {
        __asm__ volatile("" : : "r"(scene) : "memory");
        for (size_t i = 0; i < changed; ++i)
            gr_layer_set_opacity(layers[i], frame % 2 ? 1 : 0.5f);
        gr_scene_commit(scene);
    }
    unsigned long long update_ns = now() - start;
    size_t allocations = bench_count - count;
    for (size_t i = 0; i < n; ++i)
        gr_layer_unref(layers[i]);
    free(layers);
    gr_scene_unref(scene);
    printf("%zu,%zu,%zu,%llu,%llu,%zu,%zu,%lld\n", n, changed, rounds, create_ns, update_ns, live - baseline,
           allocations, (long long)bench_live - (long long)baseline);
    return 0;
}
