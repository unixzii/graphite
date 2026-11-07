#include "demo.h"

#include <graphite/gtk.h>

typedef struct
{
    demo *content;
    GMainLoop *loop;
    guint tick_id;
    unsigned frame, fps_ticks, unflushed[60], unflushed_count;
    gint64 start_time, report_time;
    bool selftest, fps;
    int status;
} demo_window;

static bool init(void)
{
    if (gtk_init_check())
        return true;
    fprintf(stderr, "GTK could not be initialized: run inside a graphical session\n");
    return false;
}

static gboolean close_window(GtkWindow *window, gpointer userdata)
{
    (void)window;
    demo_window *state = userdata;
    g_main_loop_quit(state->loop);
    return TRUE;
}

static void report_fps(demo_window *state, gint64 time)
{
    ++state->fps_ticks;
    double elapsed = (double)(time - state->report_time) / G_TIME_SPAN_SECOND;
    if (elapsed < 2)
        return;
    printf("fps: %.1f\n", state->fps_ticks / elapsed);
    fflush(stdout);
    state->report_time = time;
    state->fps_ticks = 0;
}

static void finish_selftest(demo_window *state)
{
    printf("selftest: frames=%u uncommitted=[", state->frame);
    for (unsigned i = 0; i < state->unflushed_count; ++i)
        printf("%s%u", i ? ", " : "", state->unflushed[i]);
    printf("] backing_scale=%g\n", gr_scene_get_backing_scale(state->content->scene));
    fflush(stdout);
    state->status = state->unflushed_count ? 1 : 0;
    g_main_loop_quit(state->loop);
}

static gboolean tick(GtkWidget *widget, GdkFrameClock *clock, gpointer userdata)
{
    (void)widget;
    demo_window *state = userdata;
    gint64 time = gdk_frame_clock_get_frame_time(clock);
    if (state->frame == 0)
        state->start_time = state->report_time = time;
    ++state->frame;
    if (state->selftest && state->frame > 1 && gr_scene_needs_commit(state->content->scene))
        state->unflushed[state->unflushed_count++] = state->frame;

    /* The backend commits in layout after this update-phase tick. */
    demo_update(state->content, (double)(time - state->start_time) / G_TIME_SPAN_SECOND);
    if (state->fps)
        report_fps(state, time);
    if (state->selftest && state->frame == 60)
    {
        state->tick_id = 0;
        finish_selftest(state);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

gr_text_system *demo_platform_text_system(void)
{
    return gr_gtk_text_system_new();
}

int demo_platform_run(demo *content, bool selftest, bool fps)
{
    if (!init())
        return 1;
    gr_gtk_host *host = gr_gtk_attach(content->scene);
    if (!host)
    {
        fprintf(stderr, "cannot attach GTK backend\n");
        return 1;
    }
    GtkWidget *widget = gr_gtk_host_get_widget(host);
    GtkWindow *window = GTK_WINDOW(gtk_window_new());
    g_object_ref_sink(window);
    gr_size size = demo_size(content->kind);
    gtk_window_set_title(window, "graphite demo (GTK)");
    gtk_window_set_default_size(window, (int)size.width, (int)size.height);
    gtk_window_set_child(window, widget);

    demo_window state = {.content = content, .loop = g_main_loop_new(NULL, FALSE), .selftest = selftest, .fps = fps};
    gulong close_handler = g_signal_connect(window, "close-request", G_CALLBACK(close_window), &state);
    state.tick_id = gtk_widget_add_tick_callback(widget, tick, &state, NULL);
    gtk_window_present(window);
    g_main_loop_run(state.loop);

    if (state.tick_id)
        gtk_widget_remove_tick_callback(widget, state.tick_id);
    g_signal_handler_disconnect(window, close_handler);
    gtk_window_destroy(window);
    g_object_unref(window);
    gr_gtk_host_unref(host);
    g_main_loop_unref(state.loop);
    return state.status;
}

static gr_gtk_renderer *snapshot_renderer(void)
{
    const char *selection = g_getenv("GSK_RENDERER");
    gr_gtk_renderer_kind kind = GR_GTK_VULKAN;
    bool fallback = true;
    if (selection && strcmp(selection, "cairo") == 0)
    {
        kind = GR_GTK_CAIRO;
        fallback = false;
    }
    else if (selection &&
             (strcmp(selection, "gl") == 0 || strcmp(selection, "ngl") == 0 || strcmp(selection, "opengl") == 0))
    {
        kind = GR_GTK_GL;
        fallback = false;
    }
    GError *error = NULL;
    gr_gtk_renderer *renderer = gr_gtk_renderer_new(kind, &error);
    if (!renderer && fallback)
    {
        g_clear_error(&error);
        renderer = gr_gtk_renderer_new(GR_GTK_GL, &error);
    }
    if (!renderer)
        fprintf(stderr, "cannot create snapshot renderer: %s\n", error ? error->message : "unavailable");
    g_clear_error(&error);
    return renderer;
}

bool demo_platform_snapshot(demo *content, const char *path)
{
    if (!init())
        return false;
    gr_gtk_host *host = gr_gtk_attach_offscreen(content->scene);
    if (!host)
        return false;
    gr_gtk_renderer *renderer = snapshot_renderer();
    bool ok = false;
    gr_gtk_pixels pixels = {0};
    if (renderer && gr_scene_set_backing_scale(content->scene, 1) == GR_OK &&
        gr_scene_commit(content->scene) == GR_OK && gr_gtk_renderer_render(renderer, host, 1, &pixels))
    {
        GBytes *bytes = g_bytes_new(pixels.data, (size_t)pixels.width * pixels.height * 4);
        /* Preserve premultiplied readback bytes verbatim. */
        GdkTexture *texture = gdk_memory_texture_new((int)pixels.width, (int)pixels.height, GDK_MEMORY_R8G8B8A8, bytes,
                                                     (size_t)pixels.width * 4);
        ok = gdk_texture_save_to_png(texture, path);
        g_object_unref(texture);
        g_bytes_unref(bytes);
    }
    gr_gtk_pixels_clear(&pixels);
    gr_gtk_renderer_unref(renderer);
    gr_gtk_host_unref(host);
    if (!ok)
        fprintf(stderr, "cannot render or write %s\n", path);
    return ok;
}
