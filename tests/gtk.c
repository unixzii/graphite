#include "internal.h"
#include "test.h"

#include <graphite/backend.h>
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
    gr_scene_host *host = gr_gtk_attach(scene);
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
    CHECK_NEAR(gr_scene_get_backing_scale(scene), gdk_surface_get_scale(surface));
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
    gr_scene_host_unref(host);
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
    gr_scene_host *host = gr_gtk_attach(scene);
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
    gr_scene_host_unref(host);
    gr_layer_unref(layer);
    gr_scene_unref(scene);
    iterate();
}

typedef struct
{
    gr_scene *scene;
    gr_layer *layer;
    unsigned frames, missed, viewports, focus_ins, focus_outs;
    gr_event last_frame;
} event_state;

static bool on_event(void *userdata, const gr_event *event)
{
    event_state *state = userdata;
    switch (event->type)
    {
        case GR_EVENT_FRAME:
            ++state->frames;
            state->last_frame = *event;
            /* Changes from the previous frame were committed in its layout phase. */
            if (state->frames > 1 && gr_scene_needs_commit(state->scene))
                ++state->missed;
            OK(gr_layer_set_position(state->layer, (gr_point){5 + state->frames, 5}));
            return true;
        case GR_EVENT_VIEWPORT:
            ++state->viewports;
            return true;
        case GR_EVENT_FOCUS_IN:
            ++state->focus_ins;
            return true;
        case GR_EVENT_FOCUS_OUT:
            ++state->focus_outs;
            return true;
        default:
            return false;
    }
}

static void wait_frames(event_state *state, unsigned frames)
{
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (state->frames < frames)
    {
        CHECK(g_get_monotonic_time() < deadline);
        iterate();
    }
}

static void widget_events(void)
{
    gr_scene *scene = gr_scene_new((gr_size){120, 80});
    gr_scene_host *offscreen = gr_gtk_attach_offscreen(scene);
    CHECK(offscreen && !gr_scene_host_is_interactive(offscreen));
    gr_scene_host_unref(offscreen);
    gr_scene_unref(scene);

    scene = gr_scene_new((gr_size){120, 80});
    event_state state = {.scene = scene, .layer = red_layer(scene)};
    gr_scene_host *host = gr_gtk_attach(scene);
    CHECK(host && gr_scene_host_is_interactive(host));
    gr_viewport_event viewport;
    CHECK(!gr_scene_host_get_viewport(host, &viewport)); /* Reported on first allocation. */
    gr_event_handler handler = {&state, on_event, NULL};
    OK(gr_scene_host_set_event_handler(host, &handler));
    OK(gr_scene_host_request_frame(host)); /* Before realization: delivered once the widget has a frame clock. */
    GtkWidget *widget = gr_gtk_host_get_widget(host), *window = gtk_window_new();
    g_object_ref_sink(window);
    gtk_window_set_child(GTK_WINDOW(window), widget);
    gtk_window_present(GTK_WINDOW(window));
    wait_frames(&state, 1);
    wait_clean(scene, widget);
    CHECK(state.viewports >= 1 && gr_scene_host_get_viewport(host, &viewport));
    CHECK(viewport.size.width == gtk_widget_get_width(widget) && viewport.size.height == gtk_widget_get_height(widget));
    GdkSurface *surface = gtk_native_get_surface(gtk_widget_get_native(widget));
    CHECK(viewport.scale == gdk_surface_get_scale(surface));

    /* A frame per request, each committed before the next, in the frame clock's time domain. */
    for (unsigned i = 2; i <= 10; ++i)
    {
        OK(gr_scene_host_request_frame(host));
        wait_frames(&state, i);
    }
    gr_frame_event frame = state.last_frame.value.frame;
    CHECK(state.last_frame.time <= gr_scene_host_get_time(host));
    CHECK(frame.interval > 0 && frame.interval < 0.5 && frame.target_time > state.last_frame.time);
    gint64 settle = g_get_monotonic_time() + 200000;
    while (g_get_monotonic_time() < settle)
        iterate();
    CHECK(state.frames == 10 && state.missed == 0);

    /* Focus follows the focus widget of the active window. */
    OK(gr_scene_host_request_focus(host));
    iterate();
    CHECK(gtk_widget_has_focus(widget));
    bool active = gtk_window_is_active(GTK_WINDOW(window));
    CHECK(gr_scene_host_has_focus(host) == active && state.focus_ins == (active ? 1u : 0u));
    gtk_widget_set_focusable(widget, FALSE); /* Moves focus away. */
    gtk_window_set_focus(GTK_WINDOW(window), NULL);
    iterate();
    CHECK(!gr_scene_host_has_focus(host) && state.focus_outs == state.focus_ins);
    gtk_widget_set_focusable(widget, TRUE);

    /* Cursors map to CSS cursor names. */
    OK(gr_scene_host_set_cursor(host, GR_CURSOR_TEXT));
    CHECK(gtk_widget_get_cursor(widget) && strcmp(gdk_cursor_get_name(gtk_widget_get_cursor(widget)), "text") == 0);
    OK(gr_scene_host_set_cursor(host, GR_CURSOR_RESIZE_NWSE));
    CHECK(strcmp(gdk_cursor_get_name(gtk_widget_get_cursor(widget)), "nwse-resize") == 0);
    OK(gr_scene_host_set_cursor(host, GR_CURSOR_DEFAULT));
    CHECK(!gtk_widget_get_cursor(widget));

    /* A request outstanding at release stops with the host; the widget keeps rendering. */
    OK(gr_scene_host_request_frame(host));
    gr_scene_host_unref(host);
    settle = g_get_monotonic_time() + 200000;
    while (g_get_monotonic_time() < settle)
        iterate();
    CHECK(state.frames == 10);
    OK(gr_layer_set_position(state.layer, (gr_point){30, 30}));
    wait_clean(scene, widget);
    gtk_window_destroy(GTK_WINDOW(window));
    g_object_unref(window);
    gr_layer_unref(state.layer);
    gr_scene_unref(scene);
    iterate();
}

/* An input method context driven by the test: a set preedit string, and records of what the widget asks of it. */
typedef struct
{
    GtkIMContext parent_instance;
    char *preedit;
    int cursor, resets, focus_ins, focus_outs;
    GdkRectangle area;
} TestIM;

typedef struct
{
    GtkIMContextClass parent_class;
} TestIMClass;
G_DEFINE_TYPE(TestIM, test_im, GTK_TYPE_IM_CONTEXT)

static void test_im_preedit(GtkIMContext *context, char **text, PangoAttrList **attrs, int *cursor)
{
    TestIM *im = (TestIM *)context;
    if (text)
        *text = g_strdup(im->preedit ? im->preedit : "");
    if (attrs)
        *attrs = pango_attr_list_new();
    if (cursor)
        *cursor = im->cursor;
}

/* Some input methods commit their composition when reset. */
static void test_im_reset(GtkIMContext *context)
{
    ++((TestIM *)context)->resets;
    g_signal_emit_by_name(context, "commit", "x");
}

static void test_im_focus_in(GtkIMContext *context)
{
    ++((TestIM *)context)->focus_ins;
}

static void test_im_focus_out(GtkIMContext *context)
{
    ++((TestIM *)context)->focus_outs;
}

static void test_im_cursor_location(GtkIMContext *context, GdkRectangle *area)
{
    ((TestIM *)context)->area = *area;
}

static void test_im_finalize(GObject *object)
{
    g_free(((TestIM *)object)->preedit);
    G_OBJECT_CLASS(test_im_parent_class)->finalize(object);
}

static void test_im_class_init(TestIMClass *klass)
{
    GtkIMContextClass *im = GTK_IM_CONTEXT_CLASS(klass);
    im->get_preedit_string = test_im_preedit;
    im->reset = test_im_reset;
    im->focus_in = test_im_focus_in;
    im->focus_out = test_im_focus_out;
    im->set_cursor_location = test_im_cursor_location;
    G_OBJECT_CLASS(klass)->finalize = test_im_finalize;
}

static void test_im_init(TestIM *im)
{
    (void)im;
}

static void set_preedit(TestIM *im, const char *text, int cursor)
{
    g_free(im->preedit);
    im->preedit = g_strdup(text);
    im->cursor = cursor;
    g_signal_emit_by_name(im, "preedit-changed");
}

/* A minimal text field model: applies edits, records commands and shares its state. */
typedef struct
{
    gr_scene_host *host;
    char text[64];
    size_t length;
    gr_text_range selection, composition;
    gr_text_input_kind kind;
    unsigned edits;
} field;

static void share(field *f)
{
    gr_text_input input = {.kind = f->kind,
                           .text = {f->text, f->length},
                           .selection = f->selection,
                           .composition = f->composition,
                           .caret = {{10, 20}, {1, 16}}};
    OK(gr_scene_host_set_text_input(f->host, &input));
}

static bool on_field_event(void *userdata, const gr_event *event)
{
    field *f = userdata;
    if (event->type != GR_EVENT_TEXT_EDIT)
        return false;
    const gr_text_edit_event *e = &event->value.edit;
    size_t removed = e->replace.end - e->replace.start;
    CHECK(e->replace.end <= f->length && f->length - removed + e->text.count < sizeof(f->text));
    memmove(f->text + e->replace.start + e->text.count, f->text + e->replace.end, f->length - e->replace.end);
    memcpy(f->text + e->replace.start, e->text.data, e->text.count);
    f->length = f->length - removed + e->text.count;
    f->selection = e->selection;
    f->composition = e->composition;
    ++f->edits;
    share(f);
    return true;
}

static bool field_is(const field *f, const char *text, size_t caret)
{
    return f->length == strlen(text) && memcmp(f->text, text, f->length) == 0 && f->selection.start == caret &&
           f->selection.end == caret;
}

static void widget_text_input(void)
{
    gr_scene *scene = gr_scene_new((gr_size){120, 80});
    gr_scene_host *host = gr_gtk_attach(scene);
    GtkWidget *widget = gr_gtk_host_get_widget(host), *window = gtk_window_new();
    g_object_ref_sink(window);
    gtk_window_set_child(GTK_WINDOW(window), widget);
    gtk_window_present(GTK_WINDOW(window));
    wait_clean(scene, widget);
    OK(gr_scene_host_request_focus(host));
    iterate();
    int focused = gtk_window_is_active(GTK_WINDOW(window)) ? 1 : 0;
    TestIM *im = g_object_new(test_im_get_type(), NULL);
    gr_gtk_widget_set_im_context(widget, GTK_IM_CONTEXT(im));
    field f = {.host = host};
    gr_event_handler handler = {&f, on_field_event, NULL};
    OK(gr_scene_host_set_event_handler(host, &handler));

    /* Text input is off until a field shares its state. */
    g_signal_emit_by_name(im, "commit", "zz");
    CHECK(f.edits == 0 && im->focus_ins == 0);
    share(&f);
    CHECK(im->focus_ins == focused);
    CHECK(im->area.x == 10 && im->area.y == 20 && im->area.width == 1 && im->area.height == 16);

    /* Committed text replaces the selection; a composition replaces itself. */
    g_signal_emit_by_name(im, "commit", "ab");
    CHECK(field_is(&f, "ab", 2));
    set_preedit(im, "ni", 2);
    CHECK(field_is(&f, "abni", 4) && f.composition.start == 2 && f.composition.end == 4);
    set_preedit(im, "\xE4\xBD\xA0\xE5\xA5\xBD", 1); /* 你好 with the cursor after 你. */
    CHECK(field_is(&f, "ab\xE4\xBD\xA0\xE5\xA5\xBD", 5) && f.composition.start == 2 && f.composition.end == 8);

    /* Input methods read the text around the caret without the composition. */
    char *surrounding = NULL;
    int cursor = -1, anchor = -1;
    CHECK(gtk_im_context_get_surrounding_with_selection(GTK_IM_CONTEXT(im), &surrounding, &cursor, &anchor));
    CHECK(strcmp(surrounding, "ab") == 0 && cursor == 2 && anchor == 2);
    g_free(surrounding);

    /* Ending the preedit removes it, and the commit that follows inserts the result. */
    set_preedit(im, "", 0);
    CHECK(field_is(&f, "ab", 2) && f.composition.start == f.composition.end);
    g_signal_emit_by_name(im, "commit", "\xE4\xBD\xA0\xE5\xA5\xBD");
    CHECK(field_is(&f, "ab\xE4\xBD\xA0\xE5\xA5\xBD", 8) && im->resets == 0);

    /* Deleting around the caret counts characters. */
    gboolean deleted = FALSE;
    g_signal_emit_by_name(im, "delete-surrounding", -1, 1, &deleted);
    CHECK(deleted && field_is(&f, "ab\xE4\xBD\xA0", 5));

    /* The field ending a composition itself resets the input method, ignoring what the reset commits. */
    set_preedit(im, "w", 1);
    CHECK(field_is(&f, "ab\xE4\xBD\xA0w", 6));
    unsigned edits = f.edits;
    f.composition = (gr_text_range){0, 0};
    share(&f);
    CHECK(im->resets == 1 && f.edits == edits && field_is(&f, "ab\xE4\xBD\xA0w", 6));

    /* Password fields hide their text and say so. */
    f.kind = GR_TEXT_INPUT_PASSWORD;
    share(&f);
    GtkInputPurpose purpose;
    g_object_get(im, "input-purpose", &purpose, NULL);
    CHECK(purpose == GTK_INPUT_PURPOSE_PASSWORD);
    CHECK(gtk_im_context_get_surrounding_with_selection(GTK_IM_CONTEXT(im), &surrounding, &cursor, &anchor));
    CHECK(strcmp(surrounding, "") == 0);
    g_free(surrounding);

    OK(gr_scene_host_set_text_input(host, NULL));
    CHECK(im->focus_outs == focused);
    g_signal_emit_by_name(im, "commit", "zz");
    CHECK(f.length == 6);

    gr_scene_host_unref(host);
    gtk_window_destroy(GTK_WINDOW(window));
    g_object_unref(window);
    g_object_unref(im);
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

static void ignore_host(void *data)
{
    (void)data;
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
    gr_scene_host *host = gr_gtk_attach_offscreen(scene);
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
    static const gr_scene_host_vtable foreign_vtable = {.destroy = ignore_host};
    gr_scene_host *foreign = gr_scene_host_new(scene, &foreign_vtable, NULL);
    CHECK(!gr_gtk_host_get_widget(foreign) && !gr_gtk_host_get_root_node(foreign));
    CHECK(!gr_gtk_host_get_size(foreign, &size) && size.width == 0);
    CHECK(!gr_gtk_renderer_render(renderer, foreign, 1, &invalid) && !invalid.data);
    gr_scene_host_unref(foreign);
    gr_scene_unref(scene);
    CHECK(state.destroyed == 1);
    CHECK(!gr_gtk_host_get_size(host, &size));
    CHECK(!gr_gtk_host_get_root_node(host));
    CHECK(!gr_gtk_renderer_render(renderer, host, 1, &invalid));
    gr_layer_unref(layer);
    gr_gtk_renderer_unref(renderer);
    gr_scene_host_unref(host);
}

static void empty_scene(void)
{
    gr_scene *scene = gr_scene_new((gr_size){12, 8});
    gr_scene_host *host = gr_gtk_attach_offscreen(scene);
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
    gr_scene_host_unref(host);
    gr_scene_unref(scene);
}

int main(int argc, char **argv)
{
    if (!gtk_init_check())
    {
        fprintf(stderr, "GTK needs a graphical session\n");
        return 1;
    }
    const gr_test_case cases[] = {
        GR_TEST_CASE(widget_hosting),    GR_TEST_CASE(frame_clock_commits),          GR_TEST_CASE(widget_events),
        GR_TEST_CASE(widget_text_input), GR_TEST_CASE(offscreen_cache_and_lifetime), GR_TEST_CASE(empty_scene)};
    return gr_test_run("gtk", cases, sizeof(cases) / sizeof(cases[0]), argc, argv);
}
