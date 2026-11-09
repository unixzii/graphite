#include "test.h"

#include <graphite/graphite.h>
#include <graphite/web.h>

#include <emscripten/emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* tests/web.js */
char *web_test_layer_tree(void);
int web_test_host_count(void);
void web_test_register_element(const char *name);
int web_test_hosts_in(const char *name);
bool web_test_pointer(const char *type, double x, double y, int button, int buttons, const char *kind, double pressure);
bool web_test_wheel(double x, double y, double dx, double dy, int mode);
bool web_test_key(const char *type, const char *code, const char *key, bool shift, bool meta, bool composing,
                  bool repeat);
void web_test_focus(bool focused);
char *web_test_cursor(void);
void web_test_set_scale(double scale);
char *web_test_host_text(void);
char *web_test_field(void);
void web_test_type(const char *text);
void web_test_compose(const char *type, const char *text);
bool web_test_field_key(const char *code, const char *key, int modifiers);
bool web_test_host_focused(void);
bool web_test_apple(void);

static bool tree_is(const char *expected)
{
    char *tree = web_test_layer_tree();
    bool same = strcmp(tree, expected) == 0;
    if (!same)
        fprintf(stderr, "layer tree %s, expected %s\n", tree, expected);
    free(tree);
    return same;
}

static gr_layer *add(gr_scene *scene, gr_layer *parent)
{
    gr_layer *layer = gr_layer_new(scene, GR_LAYER_CONTAINER);
    OK(gr_layer_add_child(parent, layer));
    return layer;
}

static void attach_rejects_bad_arguments(void)
{
    gr_scene *scene = gr_scene_new((gr_size){100, 100});
    CHECK(!gr_web_attach_to_element(NULL, "#host"));
    CHECK(!gr_web_attach_to_element(scene, NULL));
    CHECK(!gr_web_attach_to_element(scene, "#missing"));
    CHECK(!gr_web_attach_to_element(scene, "#bad["));
    CHECK(web_test_host_count() == 0);
    gr_scene_host *host = gr_web_attach_to_element(scene, "#host");
    CHECK(host);
    CHECK(!gr_web_attach_to_element(scene, "#host"));
    CHECK(web_test_host_count() == 1);
    gr_scene_host_unref(host);
    CHECK(web_test_host_count() == 1);
    gr_scene_unref(scene);
    CHECK(web_test_host_count() == 0);
}

static void attach_to_registered_element(void)
{
    web_test_register_element("!exact");
    gr_scene *scene = gr_scene_new((gr_size){100, 100});
    /* specialHTMLTargets also holds document and window under "1" and "2"; they are not elements. */
    CHECK(!gr_web_attach_to_element(scene, "1"));
    CHECK(!gr_web_attach_to_element(scene, "2"));
    CHECK(!gr_web_attach_to_element(scene, "!unregistered"));
    gr_scene_host *host = gr_web_attach_to_element(scene, "!exact");
    CHECK(host);
    CHECK(web_test_hosts_in("!exact") == 1);
    CHECK(web_test_host_count() == 1);
    gr_scene_host_unref(host);
    gr_scene_unref(scene);
    CHECK(web_test_hosts_in("!exact") == 0);
}

static void commits_mirror_the_tree(void)
{
    gr_scene *scene = gr_scene_new((gr_size){100, 100});
    gr_scene_host *host = gr_web_attach_to_element(scene, "#host");
    CHECK(host);
    gr_layer *root = gr_scene_get_root(scene);
    gr_layer *a = add(scene, root), *b = add(scene, root);
    gr_layer *c = add(scene, a), *d = add(scene, a);
    OK(gr_scene_commit(scene));
    CHECK(tree_is("0(1(2)(2))(1)"));

    /* Reparent within one batch: d moves under b, c is removed, b moves to the front. */
    OK(gr_layer_add_child(b, d));
    OK(gr_layer_remove_from_parent(c));
    OK(gr_layer_insert_child(root, b, 0));
    OK(gr_scene_commit(scene));
    CHECK(tree_is("0(1(2))(1)"));

    gr_layer_unref(a);
    gr_layer_unref(b);
    gr_layer_unref(c);
    gr_layer_unref(d);
    gr_scene_host_unref(host);
    gr_scene_unref(scene);
}

/* Lays out, draws and releases a layout during each draw, as labels that do not keep their layouts do. */
static void draw_temporary_text(void *userdata, gr_canvas *canvas, const gr_draw_info *info)
{
    (void)info;
    gr_text_style style = gr_text_style_default();
    gr_attributed_text *text =
        gr_attributed_text_new((gr_string){"Temporary", 9}, &style, (gr_text_span_list){NULL, 0});
    gr_text_layout_options options = gr_text_layout_options_default();
    gr_text_layout *layout = gr_text_system_layout(userdata, text, &options);
    OK(gr_canvas_draw_text_layout(canvas, layout, (gr_point){0, 0}, (gr_color){0, 0, 0, 1}));
    gr_text_layout_unref(layout);
    gr_attributed_text_unref(text);
}

static void text_from_temporary_layouts_draws(void)
{
    gr_text_system *system = gr_web_text_system_new();
    CHECK(system);
    gr_scene *scene = gr_scene_new((gr_size){100, 100});
    gr_scene_host *host = gr_web_attach_to_element(scene, "#host");
    gr_layer *label = gr_layer_new(scene, GR_LAYER_CONTENT);
    OK(gr_layer_set_frame(label, (gr_rect){{0, 0}, {100, 20}}));
    OK(gr_content_layer_set_drawer(label, &(gr_drawer){system, draw_temporary_text, NULL}));
    OK(gr_layer_add_child(gr_scene_get_root(scene), label));
    /* The display list keeps the layout alive until the runtime has replayed it. */
    OK(gr_scene_commit(scene));
    char *shown = web_test_host_text();
    CHECK(strcmp(shown, "Temporary") == 0);
    free(shown);
    gr_layer_unref(label);
    gr_scene_host_unref(host);
    gr_scene_unref(scene);
    gr_text_system_unref(system);
}

typedef struct
{
    bool consume;
    size_t count;
    gr_event last, previous;
} recorder;

static bool record(void *userdata, const gr_event *event)
{
    recorder *r = userdata;
    r->previous = r->last;
    r->last = *event;
    ++r->count;
    return r->consume;
}

static void events_reach_the_handler(void)
{
    gr_scene *scene = gr_scene_new((gr_size){100, 100});
    gr_scene_host *host = gr_web_attach_to_element(scene, "#host");
    CHECK(host && gr_scene_host_is_interactive(host));
    /* Attaching reports the host element's size (400 x 300 in the test page) before any handler is installed. */
    gr_viewport_event viewport;
    CHECK(gr_scene_host_get_viewport(host, &viewport));
    CHECK(viewport.size.width == 400 && viewport.size.height == 300);
    CHECK(viewport.scale == gr_scene_get_backing_scale(scene));
    recorder r = {.consume = true};
    OK(gr_scene_host_set_event_handler(host, &(gr_event_handler){&r, record, NULL}));
    const gr_pointer_event *p = &r.last.value.pointer;

    /* A click: down, a drag move, then up, which also reports the crossing deferred during the drag. */
    CHECK(web_test_pointer("pointerdown", 10, 20, 0, 1, "mouse", 0.5));
    CHECK(r.last.type == GR_EVENT_POINTER_DOWN && p->kind == GR_POINTER_MOUSE && p->id == 0);
    CHECK(p->position.x == 10 && p->position.y == 20);
    CHECK(p->button == GR_BUTTON_PRIMARY && p->buttons == 1u << GR_BUTTON_PRIMARY && p->click_count == 1);
    CHECK(p->pressure == 0.5f);
    CHECK(web_test_pointer("pointermove", 30, 40, -1, 1, "mouse", 0.5));
    CHECK(r.last.type == GR_EVENT_POINTER_MOVE && p->position.x == 30 && p->buttons == 1u << GR_BUTTON_PRIMARY);
    size_t before = r.count;
    web_test_pointer("pointerup", 30, 40, 0, 0, "mouse", 0);
    CHECK(r.count == before + 2 && r.previous.type == GR_EVENT_POINTER_UP);
    CHECK(r.previous.value.pointer.button == GR_BUTTON_PRIMARY && r.previous.value.pointer.buttons == 0);
    CHECK(r.last.type == GR_EVENT_POINTER_ENTER);

    /* A second press near the first counts as a double click; the secondary button counts its own clicks. */
    web_test_pointer("pointerdown", 12, 21, 0, 1, "mouse", 0.5);
    CHECK(p->click_count == 2);
    web_test_pointer("pointerup", 12, 21, 0, 0, "mouse", 0);
    web_test_pointer("pointerdown", 31, 41, 2, 2, "mouse", 0.5);
    CHECK(p->button == GR_BUTTON_SECONDARY && p->buttons == 1u << GR_BUTTON_SECONDARY && p->click_count == 1);
    web_test_pointer("pointerup", 31, 41, 2, 0, "mouse", 0);

    /* Pens report their own id and pressure; touch is not reported yet. */
    web_test_pointer("pointerdown", 5, 5, 0, 1, "pen", 0.25);
    CHECK(p->kind == GR_POINTER_PEN && p->id == 7 && p->pressure == 0.25f);
    web_test_pointer("pointerup", 5, 5, 0, 0, "pen", 0);
    before = r.count;
    CHECK(!web_test_pointer("pointerdown", 5, 5, 0, 1, "touch", 0.5));
    CHECK(r.count == before);
    web_test_pointer("pointerleave", -5, -5, -1, 0, "mouse", 0);
    CHECK(r.last.type == GR_EVENT_POINTER_LEAVE);

    /* Wheels: pixel deltas are points, line and page deltas lines. */
    CHECK(web_test_wheel(5, 6, 0, 12.5, 0));
    CHECK(r.last.type == GR_EVENT_SCROLL && r.last.value.scroll.unit == GR_SCROLL_UNIT_POINT);
    CHECK(r.last.value.scroll.delta.y == 12.5 && r.last.value.scroll.position.x == 5);
    web_test_wheel(5, 6, 0, 3, 1);
    CHECK(r.last.value.scroll.unit == GR_SCROLL_UNIT_LINE && r.last.value.scroll.delta.y == 3);

    /* Keys: physical codes, named keys, the unmodified character and modifiers after the event. */
    const gr_key_event *k = &r.last.value.key;
    CHECK(web_test_key("keydown", "KeyA", "A", true, false, false, false));
    CHECK(r.last.type == GR_EVENT_KEY_DOWN && k->code == GR_KEY_CODE_A && k->key == GR_KEY_CHARACTER);
    CHECK(k->character == 'a' && r.last.modifiers == GR_MODIFIER_SHIFT && !k->repeat);
    web_test_key("keydown", "KeyA", "A", true, false, false, true);
    CHECK(k->repeat);
    web_test_key("keyup", "KeyA", "a", false, false, false, false);
    CHECK(r.last.type == GR_EVENT_KEY_UP && r.last.modifiers == 0);
    web_test_key("keydown", "Enter", "Enter", false, false, false, false);
    CHECK(k->code == GR_KEY_CODE_ENTER && k->key == GR_KEY_ENTER && k->character == 0);
    web_test_key("keydown", "MetaLeft", "Meta", false, true, false, false);
    CHECK(k->code == GR_KEY_CODE_META_LEFT && k->key == GR_KEY_SUPER && r.last.modifiers == GR_MODIFIER_SUPER);
    web_test_key("keydown", "F13", "F13", false, false, false, false);
    CHECK(k->code == GR_KEY_CODE_F13 && k->key == GR_KEY_F13);
    web_test_key("keydown", "KeyK", "Process", false, false, true, false);
    CHECK(k->key == GR_KEY_PROCESS && k->composing);
    web_test_key("keydown", "OSLeft", "OS", false, true, false, false);
    CHECK(k->code == GR_KEY_CODE_META_LEFT && k->key == GR_KEY_SUPER);

    /* Unconsumed events keep their default action. */
    r.consume = false;
    CHECK(!web_test_key("keydown", "Tab", "Tab", false, false, false, false));
    CHECK(k->key == GR_KEY_TAB);

    /* Presses took focus; core drops reports that repeat the current state. */
    CHECK(gr_scene_host_has_focus(host));
    web_test_focus(false);
    CHECK(r.last.type == GR_EVENT_FOCUS_OUT && !gr_scene_host_has_focus(host));
    web_test_focus(true);
    CHECK(r.last.type == GR_EVENT_FOCUS_IN && gr_scene_host_has_focus(host));

    OK(gr_scene_host_set_cursor(host, GR_CURSOR_GRAB));
    char *cursor = web_test_cursor();
    CHECK(strcmp(cursor, "grab") == 0);
    free(cursor);
    CHECK(gr_scene_host_get_time(host) > 0);

    /* The released host stops listening; its element stays until the Scene goes. */
    before = r.count;
    gr_scene_host_unref(host);
    web_test_pointer("pointermove", 1, 1, -1, 0, "mouse", 0);
    web_test_key("keydown", "KeyA", "a", false, false, false, false);
    CHECK(r.count == before);
    CHECK(web_test_host_count() == 1);
    gr_scene_unref(scene);
}

/* A minimal text field model: applies edits, records commands, consumes the Q key and shares its state. */
typedef struct
{
    gr_scene_host *host;
    char text[64];
    size_t length;
    gr_text_range selection, composition;
    gr_text_input_kind kind;
    gr_text_command_event commands[8];
    size_t command_count;
    unsigned focus_outs;
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
    switch (event->type)
    {
        case GR_EVENT_TEXT_EDIT:
        {
            const gr_text_edit_event *e = &event->value.edit;
            size_t removed = e->replace.end - e->replace.start;
            CHECK(e->replace.end <= f->length && f->length - removed + e->text.count < sizeof(f->text));
            memmove(f->text + e->replace.start + e->text.count, f->text + e->replace.end, f->length - e->replace.end);
            memcpy(f->text + e->replace.start, e->text.data, e->text.count);
            f->length = f->length - removed + e->text.count;
            f->selection = e->selection;
            f->composition = e->composition;
            share(f);
            return true;
        }
        case GR_EVENT_TEXT_COMMAND:
            if (f->command_count < 8)
                f->commands[f->command_count++] = event->value.command;
            return event->value.command.command != GR_TEXT_COMMAND_INSERT_TAB;
        case GR_EVENT_KEY_DOWN:
            return event->value.key.character == 'q';
        case GR_EVENT_FOCUS_OUT:
            ++f->focus_outs;
            return true;
        default:
            return false;
    }
}

static bool field_is(const field *f, const char *text, size_t caret)
{
    bool same = f->length == strlen(text) && memcmp(f->text, text, f->length) == 0 && f->selection.start == caret &&
                f->selection.end == caret;
    if (!same)
        fprintf(stderr, "field \"%.*s\" %zu-%zu, expected \"%s\" at %zu\n", (int)f->length, f->text, f->selection.start,
                f->selection.end, text, caret);
    return same;
}

static bool hidden_field_is(const char *expected)
{
    char *state = web_test_field();
    bool same = strcmp(state, expected) == 0;
    if (!same)
        fprintf(stderr, "hidden field %s, expected %s\n", state, expected);
    free(state);
    return same;
}

static void text_input_mirrors_a_hidden_field(void)
{
    gr_scene *scene = gr_scene_new((gr_size){100, 100});
    gr_scene_host *host = gr_web_attach_to_element(scene, "#host");
    field f = {.host = host};
    OK(gr_scene_host_set_event_handler(host, &(gr_event_handler){&f, on_field_event, NULL}));
    web_test_pointer("pointerdown", 10, 20, 0, 1, "mouse", 0.5);
    web_test_pointer("pointerup", 10, 20, 0, 0, "mouse", 0);
    CHECK(gr_scene_host_has_focus(host) && hidden_field_is(""));

    /* Turning text input on puts a focused field at the caret; focus stays with the host. */
    share(&f);
    CHECK(hidden_field_is("textarea||0|0|10,20|true"));
    CHECK(web_test_host_focused() && gr_scene_host_has_focus(host) && f.focus_outs == 0);

    /* Typing, surrogate pairs included, arrives as edits in UTF-8 offsets. */
    web_test_type("ab");
    CHECK(field_is(&f, "ab", 2));
    web_test_type("\xF0\x9F\x98\x80");
    CHECK(field_is(&f, "ab\xF0\x9F\x98\x80", 6) && hidden_field_is("textarea|ab\xF0\x9F\x98\x80|4|4|10,20|true"));

    /* A composition replaces itself, then commits. */
    web_test_compose("start", "");
    web_test_compose("update", "n");
    web_test_compose("update", "ni");
    CHECK(field_is(&f, "ab\xF0\x9F\x98\x80ni", 8) && f.composition.start == 6 && f.composition.end == 8);
    web_test_compose("update", "\xE4\xBD\xA0");
    CHECK(field_is(&f, "ab\xF0\x9F\x98\x80\xE4\xBD\xA0", 9) && f.composition.start == 6 && f.composition.end == 9);
    web_test_compose("end", "\xE4\xBD\xA0");
    CHECK(field_is(&f, "ab\xF0\x9F\x98\x80\xE4\xBD\xA0", 9) && f.composition.start == f.composition.end);

    /* Editing keys become commands and keep the field as it is; Tab left unconsumed moves focus as usual. */
    CHECK(web_test_field_key("Backspace", "Backspace", 0));
    CHECK(web_test_field_key("ArrowLeft", "ArrowLeft", 1));
    int word = web_test_apple() ? 4 : 2;
    CHECK(web_test_field_key("ArrowRight", "ArrowRight", word));
    CHECK(!web_test_field_key("Tab", "Tab", 0));
    CHECK(f.command_count == 4 && f.commands[0].command == GR_TEXT_COMMAND_DELETE_BACKWARD);
    CHECK(f.commands[1].command == GR_TEXT_COMMAND_MOVE_LEFT && f.commands[1].extend);
    CHECK(f.commands[2].command == GR_TEXT_COMMAND_MOVE_WORD_RIGHT && !f.commands[2].extend);
    CHECK(f.commands[3].command == GR_TEXT_COMMAND_INSERT_TAB);
    /* A key the handler consumes types nothing; typing keys it leaves keep their default. */
    CHECK(web_test_field_key("KeyQ", "q", 0));
    CHECK(!web_test_field_key("KeyZ", "z", 0));

    /* The field follows the application's own changes, ending a composition it ends without losing focus. */
    web_test_compose("start", "");
    web_test_compose("update", "w");
    CHECK(f.composition.start == 9 && f.composition.end == 10);
    f.length = 2;
    f.selection = f.composition = (gr_text_range){1, 1};
    share(&f);
    CHECK(hidden_field_is("textarea|ab|1|1|10,20|true") && f.focus_outs == 0);
    web_test_type("x");
    CHECK(field_is(&f, "axb", 2));

    /* Password fields use a password input, which keeps input methods out. */
    f.kind = GR_TEXT_INPUT_PASSWORD;
    share(&f);
    CHECK(hidden_field_is("input password|axb|2|2|10,20|true") && f.focus_outs == 0);

    /* Turning text input off gives focus back to the host. */
    OK(gr_scene_host_set_text_input(host, NULL));
    CHECK(hidden_field_is("") && web_test_host_focused() && gr_scene_host_has_focus(host) && f.focus_outs == 0);

    gr_scene_host_unref(host);
    gr_scene_unref(scene);
}

/* Frames run in animation frames, each followed by the commit of its changes; main starts this after the cases. */
static struct
{
    gr_scene *scene;
    gr_scene_host *host;
    gr_layer *layer;
    unsigned frames;
    double last;
    bool ok;
} frames;

static void scale_changed(void *arg)
{
    (void)arg;
    gr_viewport_event viewport;
    bool ok = gr_scene_host_get_viewport(frames.host, &viewport) && viewport.scale == 2 &&
              gr_scene_get_backing_scale(frames.scene) == 2;
    printf("[ %s ] web.scale_changes_reach_the_scene\n", ok ? "      OK" : "  FAILED");
    if (!ok)
        fprintf(stderr, "viewport scale %g, backing scale %g\n", viewport.scale,
                gr_scene_get_backing_scale(frames.scene));
    gr_layer_unref(frames.layer);
    gr_scene_host_unref(frames.host);
    gr_scene_unref(frames.scene);
    emscripten_force_exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
}

static void frames_finished(void *arg)
{
    (void)arg;
    bool ok = frames.ok && frames.frames == 5 && !gr_scene_needs_commit(frames.scene) && tree_is("0(1)");
    printf("[ %s ] web.frames_precede_their_commit\n", ok ? "      OK" : "  FAILED");
    if (!ok)
        emscripten_force_exit(EXIT_FAILURE);
    /* A devicePixelRatio change (page zoom, another display) reaches the Scene and the viewport. */
    web_test_set_scale(2);
    OK(gr_scene_host_request_frame(frames.host));
    emscripten_async_call(scale_changed, NULL, 300);
}

static bool on_frame(void *userdata, const gr_event *event)
{
    (void)userdata;
    if (event->type != GR_EVENT_FRAME || frames.frames == 5)
        return event->type == GR_EVENT_FRAME;
    /* The attach's changes and every earlier frame's changes were committed by the end of their animation frame. */
    if (++frames.frames > 1)
        frames.ok &= !gr_scene_needs_commit(frames.scene);
    const gr_frame_event *f = &event->value.frame;
    frames.ok &= event->time > frames.last && f->interval > 0 && f->target_time > event->time;
    frames.last = event->time;
    OK(gr_layer_set_position(frames.layer, (gr_point){frames.frames, 0}));
    if (frames.frames < 5)
        OK(gr_scene_host_request_frame(frames.host));
    else
        emscripten_async_call(frames_finished, NULL, 50);
    return true;
}

int main(int argc, char **argv)
{
    const gr_test_case cases[] = {
        GR_TEST_CASE(attach_rejects_bad_arguments),      GR_TEST_CASE(attach_to_registered_element),
        GR_TEST_CASE(commits_mirror_the_tree),           GR_TEST_CASE(events_reach_the_handler),
        GR_TEST_CASE(text_from_temporary_layouts_draws), GR_TEST_CASE(text_input_mirrors_a_hidden_field),
    };
    int status = gr_test_run("web", cases, sizeof(cases) / sizeof(cases[0]), argc, argv);
    if (status != EXIT_SUCCESS)
        return status;
    frames.scene = gr_scene_new((gr_size){100, 100});
    frames.host = gr_web_attach_to_element(frames.scene, "#host");
    CHECK(frames.host);
    frames.layer = add(frames.scene, gr_scene_get_root(frames.scene));
    frames.ok = true;
    OK(gr_scene_host_set_event_handler(frames.host, &(gr_event_handler){NULL, on_frame, NULL}));
    OK(gr_scene_host_request_frame(frames.host));
    emscripten_exit_with_live_runtime();
}
