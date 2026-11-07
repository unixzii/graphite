#include "internal.h"

#include <errno.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#endif

static void *grow(void *data, size_t *capacity, size_t count, size_t size)
{
    if (count == SIZE_MAX || count + 1 > SIZE_MAX / size)
        abort();
    if (count < *capacity)
        return data;
    size_t next = *capacity ? *capacity : 8;
    while (next <= count)
    {
        if (next > SIZE_MAX / 2 / size)
        {
            next = count + 1;
            break;
        }
        next *= 2;
    }
    void *result = realloc(data, next * size);
    if (!result)
        abort();
    *capacity = next;
    return result;
}

void context_clear(ctx *c)
{
    for (size_t i = 0; i < c->frame_count; ++i)
    {
        free(c->frames[i]->pixels.data);
        free(c->frames[i]);
    }
    for (size_t i = 0; i < c->host_count; ++i)
        if (c->hosts[i].capture)
            c->harness->destroy(c->hosts[i].capture);
    for (size_t i = 0; i < c->layer_count; ++i)
        gr_layer_unref(c->layers[i]);
    for (size_t i = 0; i < c->scene_count; ++i)
        gr_scene_unref(c->scenes[i]);
    gr_text_system_unref(c->text);
    free(c->frames);
    free(c->hosts);
    free(c->layers);
    free(c->scenes);
    *c = (ctx){0};
}

static void mkdirs(char *path)
{
    for (char *p = path + 1; *p; ++p)
        if (*p == '/')
        {
            *p = 0;
#if defined(_WIN32)
            _mkdir(path);
#else
            mkdir(path, 0777);
#endif
            *p = '/';
        }
#if defined(_WIN32)
    _mkdir(path);
#else
    mkdir(path, 0777);
#endif
}

static uint32_t crc32(const uint8_t *data, size_t size, uint32_t crc)
{
    for (size_t i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return crc;
}

static void be32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static bool chunk(FILE *file, const char *type, const uint8_t *data, size_t size)
{
    uint8_t word[4];
    be32(word, (uint32_t)size);
    if (fwrite(word, 1, 4, file) != 4 || fwrite(type, 1, 4, file) != 4 || (size && fwrite(data, 1, size, file) != size))
        return false;
    uint32_t crc = crc32((const uint8_t *)type, 4, 0xffffffffu);
    crc = crc32(data, size, crc) ^ 0xffffffffu;
    be32(word, crc);
    return fwrite(word, 1, 4, file) == 4;
}

/* PNG rows remain premultiplied to preserve exact capture bytes.
 * Stored DEFLATE blocks avoid a codec dependency; compression is not compared. */
static bool save_png(const char *path, const gr_conf_pixels *pixels)
{
    size_t stride = (size_t)pixels->width * 4 + 1, size = stride * pixels->height;
    uint8_t *raw = malloc(size), *z = malloc(size + size / 65535 * 5 + 16);
    if (!raw || !z)
        abort();
    for (uint32_t y = 0; y < pixels->height; ++y)
    {
        raw[y * stride] = 0;
        memcpy(raw + y * stride + 1, pixels->data + (size_t)y * pixels->width * 4, stride - 1);
    }
    size_t at = 0, pos = 0;
    z[at++] = 0x78;
    z[at++] = 0x01;
    while (pos < size)
    {
        size_t n = size - pos;
        if (n > 65535)
            n = 65535;
        z[at++] = (uint8_t)(pos + n == size);
        z[at++] = (uint8_t)n;
        z[at++] = (uint8_t)(n >> 8);
        uint16_t inverse = (uint16_t)~n;
        z[at++] = (uint8_t)inverse;
        z[at++] = (uint8_t)(inverse >> 8);
        memcpy(z + at, raw + pos, n);
        at += n;
        pos += n;
    }
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < size; ++i)
    {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    be32(z + at, (b << 16) | a);
    at += 4;
    FILE *file = fopen(path, "wb");
    bool ok = false;
    if (file)
    {
        const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
        uint8_t header[13] = {0};
        be32(header, pixels->width);
        be32(header + 4, pixels->height);
        header[8] = 8;
        header[9] = 6;
        ok = fwrite(signature, 1, 8, file) == 8 && chunk(file, "IHDR", header, 13) && chunk(file, "IDAT", z, at) &&
             chunk(file, "IEND", NULL, 0);
        if (fclose(file) != 0)
            ok = false;
    }
    free(raw);
    free(z);
    return ok;
}

gr_text_system *text_system(ctx *c)
{
    if (!c->text && c->harness->text_system)
        c->text = c->harness->text_system(c->harness->userdata);
    if (!c->text)
        c->skipped = true;
    return c->text;
}

void conf_check(ctx *c, bool ok, const char *format, ...)
{
    if (ok)
        return;
    ++c->failures;
    fprintf(stderr, "[%s] %s@%gx: ", c->harness->name, c->name, c->scale);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

void expect_color(ctx *c, const frame *f, const char *label, gr_point p, rgba8 expected, unsigned tolerance)
{
    rgba8 got;
    if (!frame_at(f, p, &got))
    {
        conf_check(c, false, "%s: probe (%g, %g) is outside the frame", label, p.x, p.y);
        return;
    }
    bool ok = true;
    for (size_t i = 0; i < 4; ++i)
        ok &= abs((int)got.v[i] - (int)expected.v[i]) <= (int)tolerance;
    conf_check(c, ok, "%s: pixel at (%g, %g) is [%u, %u, %u, %u], expected [%u, %u, %u, %u]", label, p.x, p.y, got.v[0],
               got.v[1], got.v[2], got.v[3], expected.v[0], expected.v[1], expected.v[2], expected.v[3]);
}

void expect_near(ctx *c, const char *label, double measured, double expected, double tolerance)
{
    if (!isfinite(measured))
    {
        conf_check(c, false, "%s: could not be measured (expected %.3f)", label, expected);
        return;
    }
    fprintf(stderr, "[%s] %s@%gx %s: %.3f (expected %.3f ± %.3f)\n", c->harness->name, c->name, c->scale, label,
            measured, expected, tolerance);
    conf_check(c, fabs(measured - expected) <= tolerance, "%s: measured %.3f, expected %.3f ± %.3f", label, measured,
               expected, tolerance);
}

gr_scene *new_scene(ctx *c)
{
    c->scenes = grow(c->scenes, &c->scene_capacity, c->scene_count, sizeof(*c->scenes));
    gr_scene *s = gr_scene_new((gr_size){100, 100});
    if (!s)
        abort();
    c->scenes[c->scene_count++] = s;
    return s;
}

gr_layer *new_layer(ctx *c, gr_scene *s, gr_layer_kind kind)
{
    c->layers = grow(c->layers, &c->layer_capacity, c->layer_count, sizeof(*c->layers));
    gr_layer *l = gr_layer_new(s, kind);
    if (!l)
        abort();
    c->layers[c->layer_count++] = l;
    return l;
}

gr_color color(rgba8 value)
{
    return (gr_color){value.v[0] / 255.0f, value.v[1] / 255.0f, value.v[2] / 255.0f, value.v[3] / 255.0f};
}

gr_layer *solid(ctx *c, gr_scene *s, gr_rect rect, rgba8 value)
{
    gr_layer *l = new_layer(c, s, GR_LAYER_CONTAINER);
    gr_color v = color(value);
    API(gr_layer_set_frame(l, rect));
    API(gr_layer_set_background(l, &v));
    API(gr_layer_add_child(gr_scene_get_root(s), l));
    return l;
}

frame *new_frame(ctx *c)
{
    c->frames = grow(c->frames, &c->frame_capacity, c->frame_count, sizeof(*c->frames));
    frame *f = calloc(1, sizeof(*f));
    if (!f)
        abort();
    c->frames[c->frame_count++] = f;
    f->scale = c->scale;
    return f;
}

frame *render(ctx *c, gr_scene *s)
{
    host *h = NULL;
    for (size_t i = 0; i < c->host_count; ++i)
        if (c->hosts[i].scene == s)
            h = &c->hosts[i];
    if (!h)
    {
        c->hosts = grow(c->hosts, &c->host_capacity, c->host_count, sizeof(*c->hosts));
        h = &c->hosts[c->host_count++];
        h->scene = s;
        h->capture = c->harness->attach(c->harness->userdata, s, c->scale);
    }
    frame *f = new_frame(c);
    if (!h->capture || gr_scene_commit(s) != GR_OK || !c->harness->capture(h->capture, &f->pixels))
    {
        conf_check(c, false, "backend attach/commit/capture failed");
        return f;
    }
    const char *base = getenv("GRAPHITE_CONFORMANCE_OUT");
    if (!base)
        base = GRAPHITE_CONFORMANCE_OUT;
    char dir[4096], path[8192], suffix[32] = "";
    snprintf(dir, sizeof(dir), "%s/%s", base, c->harness->name);
    mkdirs(dir);
    if (c->frame_count > 1)
        snprintf(suffix, sizeof(suffix), "-%zu", c->frame_count - 1);
    snprintf(path, sizeof(path), "%s/%s@%gx%s.png", dir, c->name, c->scale, suffix);
    if (!save_png(path, &f->pixels))
        fprintf(stderr, "conformance: cannot save %s: %s\n", path, strerror(errno));
    return f;
}

gr_path *rectangle_path(gr_rect r)
{
    gr_path_builder *b = gr_path_builder_new();
    API(gr_path_builder_add_rect(b, r));
    gr_path *p = gr_path_builder_finish(b);
    gr_path_builder_unref(b);
    return p;
}

gr_path *line_path(gr_point from, gr_point to)
{
    gr_path_element e[] = {{GR_PATH_MOVE, {from}}, {GR_PATH_LINE, {to}}};
    return gr_path_new((gr_path_element_list){e, 2});
}

static gr_paint *paint(rgba8 value)
{
    gr_paint_desc d = {.kind = GR_PAINT_SOLID, .value.solid = color(value)};
    return gr_paint_new(&d);
}

void set_fill(gr_layer *l, rgba8 value)
{
    gr_paint *p = paint(value);
    API(gr_shape_layer_set_fill(l, p));
    gr_paint_unref(p);
}

void set_stroke(gr_layer *l, rgba8 value, double width)
{
    gr_paint *p = paint(value);
    gr_stroke_style style = gr_stroke_style_default();
    style.width = width;
    gr_stroke *stroke = gr_stroke_new(p, &style);
    API(gr_shape_layer_set_stroke(l, stroke));
    gr_stroke_unref(stroke);
    gr_paint_unref(p);
}

void canvas_fill(gr_canvas *canvas, gr_rect r, rgba8 value)
{
    gr_paint *p = paint(value);
    API(gr_canvas_fill_rect(canvas, r, p));
    gr_paint_unref(p);
}

void set_gradient(gr_layer *l, bool radial, gr_point start, gr_point end, double radius)
{
    gr_gradient_stop stops[] = {{0, color(RED)}, {1, color(BLUE)}};
    gr_paint_desc d = {.kind = radial ? GR_PAINT_RADIAL : GR_PAINT_LINEAR};
    if (radial)
    {
        d.value.radial.center = start;
        d.value.radial.radius = radius;
        d.value.radial.stops = (gr_gradient_stop_list){stops, 2};
    }
    else
    {
        d.value.linear.start = start;
        d.value.linear.end = end;
        d.value.linear.stops = (gr_gradient_stop_list){stops, 2};
    }
    gr_paint *p = gr_paint_new(&d);
    API(gr_gradient_layer_set_gradient(l, p));
    gr_paint_unref(p);
}

void set_shadow(gr_layer *l, double x, double y, double sigma, double spread)
{
    gr_shadow shadow = {.color = color(BLACK), .offset = {x, y}, .blur_sigma = sigma, .spread = spread};
    API(gr_layer_set_shadow(l, &shadow));
}

int gr_conf_run_main(const gr_conf_harness *harnesses, size_t count, int argc, char **argv)
{
    size_t selected = 0, failed = 0, skipped = 0;
    for (size_t h = 0; h < count; ++h)
        for (size_t i = 0; i < gr_conf_case_count; ++i)
        {
            const conf_case *test = &gr_conf_cases[i];
            char full[512];
            snprintf(full, sizeof(full), "%s::%s", harnesses[h].name, test->name);
            bool any = false, match = false;
            for (int a = 1; a < argc; ++a)
                if (argv[a][0] != '-')
                {
                    any = true;
                    match |= strstr(full, argv[a]) != NULL;
                }
            if (any && !match)
                continue;
            ++selected;
            size_t failures = 0;
            bool skip = false;
            printf("[ RUN      ] %s (%s)\n", full, test->tier);
            fflush(stdout);
            const double scales[] = {1, 1.5, 2};
            for (size_t scale = 0; scale < 3 && !skip; ++scale)
            {
                ctx c = {.harness = &harnesses[h], .name = test->name, .scale = scales[scale]};
                test->run(&c);
                failures += c.failures;
                skip = c.skipped;
                context_clear(&c);
            }
            printf("test %s ... %s\n", full, failures ? "FAILED" : skip ? "skipped" : "ok");
            fflush(stdout);
            failed += failures != 0;
            skipped += skip && !failures;
        }
    printf("\ntest result: %s. %zu passed; %zu failed; %zu skipped\n", failed ? "FAILED" : "ok",
           selected - failed - skipped, failed, skipped);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
