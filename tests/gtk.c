#include "test.h"
#include <graphite/gtk.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static gr_layer *red_layer(gr_scene *scene)
{
    gr_layer *layer = gr_layer_new(scene, GR_LAYER_CONTAINER);
    gr_color red = {1, 0, 0, 1};
    OK(gr_layer_set_frame(layer, (gr_rect){{0, 0}, {10, 10}}));
    OK(gr_layer_set_background(layer, &red));
    OK(gr_layer_add_child(gr_scene_get_root(scene), layer));
    return layer;
}

static void iterate(void)
{
    GMainContext *context = g_main_context_default();
    while (g_main_context_pending(context))
        g_main_context_iteration(context, FALSE);
    g_usleep(5000);
}

static void wait_clean(gr_scene *scene, GtkWidget *widget)
{
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (gr_scene_needs_commit(scene) || gtk_widget_get_width(widget) <= 0)
    {
        CHECK(g_get_monotonic_time() < deadline);
        iterate();
    }
}

static GBytes *paint_widget(GtkWidget *widget, size_t *stride)
{
    int w = gtk_widget_get_width(widget), h = gtk_widget_get_height(widget);
    GtkSnapshot *snapshot = gtk_snapshot_new();
    GdkPaintable *paintable = gtk_widget_paintable_new(widget);
    gdk_paintable_snapshot(paintable, GDK_SNAPSHOT(snapshot), w, h);
    g_object_unref(paintable);
    GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
    CHECK(node);
    GskRenderer *renderer = gsk_cairo_renderer_new();
    GError *error = NULL;
    CHECK(gsk_renderer_realize_for_display(renderer, gdk_display_get_default(), &error));
    CHECK(!error);
    graphene_rect_t viewport;
    graphene_rect_init(&viewport, 0, 0, w, h);
    GdkTexture *texture = gsk_renderer_render_texture(renderer, node, &viewport);
    gsk_render_node_unref(node);
    GdkTextureDownloader *downloader = gdk_texture_downloader_new(texture);
    gdk_texture_downloader_set_format(downloader, GDK_MEMORY_R8G8B8A8_PREMULTIPLIED);
    GBytes *bytes = gdk_texture_downloader_download_bytes(downloader, stride);
    gdk_texture_downloader_free(downloader);
    g_object_unref(texture);
    gsk_renderer_unrealize(renderer);
    g_object_unref(renderer);
    return bytes;
}

static void expect_pixel(GBytes *bytes, size_t stride, unsigned x, unsigned y, bool red)
{
    gsize length;
    const uint8_t *data = g_bytes_get_data(bytes, &length);
    size_t i = y * stride + x * 4;
    CHECK(i + 4 <= length);
    if (red)
        CHECK(data[i] == 255 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 255);
    else
        CHECK(data[i + 3] == 0);
}

static void widget_hosting(void)
{
    gr_scene *scene = gr_scene_new((gr_size){120, 80});
    gr_layer *layer = red_layer(scene);
    gr_gtk_host *host = gr_gtk_attach(scene);
    CHECK(host);
    GtkWidget *widget = gr_gtk_host_get_widget(host);
    CHECK(gtk_widget_get_overflow(widget) == GTK_OVERFLOW_HIDDEN);
    GtkWidget *window = gtk_window_new();
    g_object_ref_sink(window);
    gtk_window_set_child(GTK_WINDOW(window), widget);
    gtk_window_present(GTK_WINDOW(window));
    wait_clean(scene, widget);
    GtkRequisition min, natural;
    gtk_widget_get_preferred_size(widget, &min, &natural);
    CHECK(natural.width == 120 && natural.height == 80);
    GdkSurface *surface = gtk_native_get_surface(gtk_widget_get_native(widget));
    NEAR(gr_scene_get_backing_scale(scene), gdk_surface_get_scale(surface));
    size_t stride;
    GBytes *bytes = paint_widget(widget, &stride);
    expect_pixel(bytes, stride, 5, 5, true);
    expect_pixel(bytes, stride, 5, 50, false);
    g_bytes_unref(bytes);
    gint64 settle = g_get_monotonic_time() + 1500000;
    while (g_get_monotonic_time() < settle)
        iterate();
    OK(gr_layer_set_position(layer, (gr_point){65, 45}));
    wait_clean(scene, widget);
    bytes = paint_widget(widget, &stride);
    expect_pixel(bytes, stride, 65, 45, true);
    expect_pixel(bytes, stride, 5, 5, false);
    g_bytes_unref(bytes);
    gtk_window_set_child(GTK_WINDOW(window), NULL);
    gtk_window_set_child(GTK_WINDOW(window), widget);
    wait_clean(scene, widget);
    OK(gr_layer_set_position(layer, (gr_point){5, 5}));
    wait_clean(scene, widget);
    /* Native parent ownership keeps widget/shared state alive after host release. */
    gr_gtk_host_unref(host);
    OK(gr_layer_set_opacity(layer, 0.5f));
    wait_clean(scene, widget);
    gr_scene_unref(scene);
    gr_layer_unref(layer);
    gtk_window_destroy(GTK_WINDOW(window));
    g_object_unref(window);
    iterate();
}

typedef struct
{
    gr_scene *scene;
    gr_layer *layer;
    unsigned frames, missed;
} tick_state;

static gboolean tick(GtkWidget *widget, GdkFrameClock *clock, gpointer userdata)
{
    (void)widget;
    (void)clock;
    tick_state *state = userdata;
    ++state->frames;
    if (state->frames > 1 && gr_scene_needs_commit(state->scene))
        ++state->missed;
    OK(gr_layer_set_position(state->layer, (gr_point){5 + (state->frames % 50), 5}));
    return state->frames < 60;
}

static void frame_clock_commits(void)
{
    gr_scene *scene = gr_scene_new((gr_size){120, 80});
    gr_layer *layer = red_layer(scene);
    gr_gtk_host *host = gr_gtk_attach(scene);
    CHECK(host);
    GtkWidget *widget = gr_gtk_host_get_widget(host), *window = gtk_window_new();
    g_object_ref_sink(window);
    gtk_window_set_child(GTK_WINDOW(window), widget);
    tick_state state = {scene, layer, 0, 0};
    gtk_widget_add_tick_callback(widget, tick, &state, NULL);
    gtk_window_present(GTK_WINDOW(window));
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (state.frames < 60 || gr_scene_needs_commit(scene))
    {
        CHECK(g_get_monotonic_time() < deadline);
        iterate();
    }
    CHECK(state.missed == 0);
    gtk_window_destroy(GTK_WINDOW(window));
    g_object_unref(window);
    gr_gtk_host_unref(host);
    gr_layer_unref(layer);
    gr_scene_unref(scene);
    iterate();
}

typedef struct
{
    unsigned draws, destroyed;
} draw_state;

static void draw(void *userdata, gr_canvas *canvas, const gr_draw_info *info)
{
    draw_state *state = userdata;
    ++state->draws;
    gr_paint_desc desc = {.kind = GR_PAINT_SOLID, .value.solid = {1, 0, 0, 1}};
    gr_paint *p = gr_paint_new(&desc);
    OK(gr_canvas_fill_rect(canvas, info->bounds, p));
    gr_paint_unref(p);
}

static void destroy_draw(void *userdata)
{
    ++((draw_state *)userdata)->destroyed;
}

static void offscreen_cache_and_lifetime(void)
{
    gr_scene *scene = gr_scene_new((gr_size){100, 100});
    gr_layer *layer = gr_layer_new(scene, GR_LAYER_CONTENT);
    OK(gr_layer_set_frame(layer, (gr_rect){{0, 0}, {10, 10}}));
    OK(gr_layer_add_child(gr_scene_get_root(scene), layer));
    draw_state state = {0};
    gr_drawer drawer = {&state, draw, destroy_draw};
    OK(gr_content_layer_set_drawer(layer, &drawer));
    gr_gtk_host *host = gr_gtk_attach_offscreen(scene);
    CHECK(host && !gr_gtk_host_get_widget(host));
    CHECK(!gr_gtk_attach_offscreen(scene));
    gr_size size;
    CHECK(!gr_gtk_host_get_size(host, &size));
    OK(gr_scene_commit(scene));
    CHECK(state.draws == 1);
    CHECK(gr_gtk_host_get_size(host, &size) && size.width == 100);
    GskRenderNode *a = gr_gtk_host_get_root_node(host), *b = gr_gtk_host_get_root_node(host);
    CHECK(a && a == b);
    gsk_render_node_unref(b);
    GError *error = NULL;
    gr_gtk_renderer *renderer = gr_gtk_renderer_new(GR_GTK_CAIRO, &error);
    CHECK(renderer && !error);
    for (unsigned pass = 0; pass < 2; ++pass)
    {
        gr_gtk_pixels pixels = {0};
        CHECK(gr_gtk_renderer_render(renderer, host, 2, &pixels));
        CHECK(pixels.width == 200 && pixels.height == 200);
        CHECK(pixels.data[(5 * 200 + 5) * 4] == 255);
        gr_gtk_pixels_clear(&pixels);
    }
    CHECK(state.draws == 1);
    OK(gr_layer_set_position(layer, (gr_point){15, 15}));
    OK(gr_scene_commit(scene));
    CHECK(state.draws == 1);
    b = gr_gtk_host_get_root_node(host);
    CHECK(b && a != b);
    gsk_render_node_unref(a);
    gsk_render_node_unref(b);
    OK(gr_content_layer_set_needs_display(layer));
    OK(gr_scene_commit(scene));
    CHECK(state.draws == 2);
    OK(gr_scene_set_backing_scale(scene, 1.5));
    OK(gr_scene_commit(scene));
    CHECK(state.draws == 3);
    gr_gtk_pixels invalid = {0};
    CHECK(!gr_gtk_renderer_render(renderer, host, NAN, &invalid));
    CHECK(!invalid.data);
    gr_scene_unref(scene);
    CHECK(state.destroyed == 1);
    CHECK(!gr_gtk_host_get_size(host, &size));
    CHECK(!gr_gtk_host_get_root_node(host));
    CHECK(!gr_gtk_renderer_render(renderer, host, 1, &invalid));
    gr_layer_unref(layer);
    gr_gtk_renderer_unref(renderer);
    gr_gtk_host_unref(host);
}

static void empty_scene(void)
{
    gr_scene *scene = gr_scene_new((gr_size){12, 8});
    gr_gtk_host *host = gr_gtk_attach_offscreen(scene);
    CHECK(host);
    OK(gr_scene_commit(scene));
    GError *error = NULL;
    gr_gtk_renderer *r = gr_gtk_renderer_new(GR_GTK_CAIRO, &error);
    CHECK(r && !error);
    gr_gtk_pixels p = {0};
    CHECK(gr_gtk_renderer_render(r, host, 1.5, &p));
    CHECK(p.width == 18 && p.height == 12);
    for (size_t i = 0; i < (size_t)p.width * p.height * 4; ++i)
        CHECK(p.data[i] == 0);
    gr_gtk_pixels_clear(&p);
    gr_gtk_renderer_unref(r);
    gr_gtk_host_unref(host);
    gr_scene_unref(scene);
}

int main(int argc, char **argv)
{
    if (!gtk_init_check())
    {
        fprintf(stderr, "GTK needs a graphical session\n");
        return 1;
    }
    const gr_test_case cases[] = {GR_TEST_CASE(widget_hosting), GR_TEST_CASE(frame_clock_commits),
                                  GR_TEST_CASE(offscreen_cache_and_lifetime), GR_TEST_CASE(empty_scene)};
    return gr_test_run("gtk", cases, sizeof(cases) / sizeof(cases[0]), argc, argv);
}
