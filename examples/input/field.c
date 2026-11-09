#include "field.h"

#define FIELD_FONT 16.0
#define FIELD_PADDING 12.0
#define FIELD_CAPACITY 1024
#define LINE_COLOR 0xD1D1D6

struct text_field
{
    example *ex;
    gr_layer *box, *content, *caret;
    gr_rect frame;
    gr_point host_origin;
    char text[FIELD_CAPACITY];
    size_t length;
    size_t anchor, focus; /* Selection ends; focus moves when extending. */
    gr_text_range composition;
    gr_text_layout *layout, *placeholder;
    double layout_y, scroll; /* scroll: how far the text is scrolled left to keep the caret in view. */
    bool focused, dragging, caret_on;
    double blink_start, now;
};

static size_t min_size(size_t a, size_t b)
{
    return a < b ? a : b;
}

static size_t max_size(size_t a, size_t b)
{
    return a > b ? a : b;
}

static gr_text_range selection(const text_field *f)
{
    return (gr_text_range){min_size(f->anchor, f->focus), max_size(f->anchor, f->focus)};
}

static gr_text_layout *lay_out(text_field *f, gr_string text)
{
    if (!f->ex->text)
        return NULL;
    gr_text_style style = gr_text_style_default();
    style.size = FIELD_FONT;
    gr_attributed_text *attributed = gr_attributed_text_new(text, &style, (gr_text_span_list){NULL, 0});
    gr_text_layout_options options = gr_text_layout_options_default();
    options.wrap = GR_TEXT_WRAP_NONE;
    gr_text_layout *layout = gr_text_system_layout(f->ex->text, attributed, &options);
    gr_attributed_text_unref(attributed);
    return layout;
}

/* Layout origin in the content layer, which starts FIELD_PADDING into the box. */
static gr_point layout_origin(const text_field *f)
{
    return EX_POINT(-f->scroll, f->layout_y);
}

static gr_rect caret_rect(const text_field *f)
{
    gr_rect caret = {{0, 0}, {0, 0}};
    if (f->layout)
        EX_OK(gr_text_layout_get_caret_rect(f->layout, (gr_text_position){f->focus, GR_TEXT_AFFINITY_DOWNSTREAM},
                                            &caret));
    return caret;
}

/* Scrolls the text as little as possible to bring the caret into view, without scrolling past its end. */
static void follow_caret(text_field *f)
{
    double visible = f->frame.size.width - 2 * FIELD_PADDING;
    double width = f->layout ? gr_text_layout_get_metrics(f->layout).size.width : 0;
    double x = caret_rect(f).origin.x;
    if (x - f->scroll > visible)
        f->scroll = x - visible;
    if (x < f->scroll)
        f->scroll = x;
    if (f->scroll > width - visible)
        f->scroll = width - visible;
    if (f->scroll < 0)
        f->scroll = 0;
}

/* The caret is a layer of its own, so blinking redraws nothing and it may overhang the text's edges. */
static void update_caret(text_field *f)
{
    gr_text_range sel = selection(f);
    bool shown = f->focused && f->caret_on && sel.start == sel.end && f->layout;
    EX_OK(gr_layer_set_hidden(f->caret, !shown));
    if (!shown)
        return;
    gr_rect caret = caret_rect(f);
    gr_point o = layout_origin(f);
    EX_OK(gr_layer_set_frame(
        f->caret, EX_RECT(FIELD_PADDING + o.x + caret.origin.x - 0.75, o.y + caret.origin.y, 1.5, caret.size.height)));
}

/* Shares the editing state with the host's text input while focused. */
static void share(text_field *f)
{
    if (!f->focused || !f->ex->host)
        return;
    gr_point origin = layout_origin(f);
    gr_text_input input = {
        .kind = GR_TEXT_INPUT_TEXT,
        .text = {f->text, f->length},
        .selection = selection(f),
        .composition = f->composition,
        .layout = f->layout,
        .origin = {f->host_origin.x + FIELD_PADDING + origin.x, f->host_origin.y + origin.y},
        .caret = {{f->host_origin.x + FIELD_PADDING, f->host_origin.y + 8}, {0, f->frame.size.height - 16}},
    };
    EX_OK(gr_scene_host_set_text_input(f->ex->host, &input));
}

/* Rebuilds the layout after a change, scrolls to the caret, restarts its blink and shares the new state. */
static void changed(text_field *f)
{
    gr_text_layout_unref(f->layout);
    f->layout = lay_out(f, (gr_string){f->text, f->length});
    if (f->layout)
        f->layout_y = (f->frame.size.height - gr_text_layout_get_metrics(f->layout).size.height) / 2;
    follow_caret(f);
    f->blink_start = f->now;
    f->caret_on = true;
    update_caret(f);
    EX_OK(gr_content_layer_set_needs_display(f->content));
    share(f);
}

static void fill_rect(gr_canvas *canvas, gr_rect r, gr_color color)
{
    gr_paint *paint = gr_paint_new(&(gr_paint_desc){.kind = GR_PAINT_SOLID, .value.solid = color});
    EX_OK(gr_canvas_fill_rect(canvas, r, paint));
    gr_paint_unref(paint);
}

static void draw_field(void *userdata, gr_canvas *canvas, const gr_draw_info *info)
{
    (void)info;
    text_field *f = userdata;
    gr_point o = layout_origin(f);
    if (!f->length && !f->focused && f->placeholder)
    {
        double y = (f->frame.size.height - gr_text_layout_get_metrics(f->placeholder).size.height) / 2;
        EX_OK(gr_canvas_draw_text_layout(canvas, f->placeholder, EX_POINT(0, y), alpha(hex(EX_INK), 0.35f)));
        return;
    }
    if (!f->layout)
        return;
    gr_rect rects[16];
    gr_text_range sel = selection(f);
    if (f->focused && sel.start < sel.end)
    {
        size_t n = min_size(gr_text_layout_get_range_rects(f->layout, sel, rects, 16), 16);
        for (size_t i = 0; i < n; ++i)
            fill_rect(
                canvas,
                EX_RECT(o.x + rects[i].origin.x, o.y + rects[i].origin.y, rects[i].size.width, rects[i].size.height),
                alpha(hex(EX_BLUE), 0.25f));
    }
    EX_OK(gr_canvas_draw_text_layout(canvas, f->layout, o, alpha(hex(EX_INK), 0.9f)));
    /* The composition is underlined until the input method commits it. */
    if (f->composition.start < f->composition.end)
    {
        size_t n = min_size(gr_text_layout_get_range_rects(f->layout, f->composition, rects, 16), 16);
        for (size_t i = 0; i < n; ++i)
            fill_rect(canvas,
                      EX_RECT(o.x + rects[i].origin.x, o.y + rects[i].origin.y + rects[i].size.height - 2,
                              rects[i].size.width, 1.5),
                      alpha(hex(EX_INK), 0.8f));
    }
}

static void free_field(void *userdata)
{
    text_field *f = userdata;
    gr_text_layout_unref(f->layout);
    gr_text_layout_unref(f->placeholder);
    free(f);
}

text_field *text_field_new(example *ex, gr_layer *parent, gr_rect frame, gr_point host_origin, const char *placeholder)
{
    text_field *f = calloc(1, sizeof(*f));
    if (!f)
        abort();
    f->ex = ex;
    f->frame = frame;
    f->host_origin = host_origin;
    f->box = pill(ex, parent, frame, EX_WHITE, 10);
    border(f->box, 1, hex(LINE_COLOR));
    EX_OK(gr_layer_set_masks_to_bounds(f->box, true));
    f->content = new_layer(ex, GR_LAYER_CONTENT);
    EX_OK(gr_layer_set_frame(f->content,
                             EX_RECT(FIELD_PADDING, 0, frame.size.width - 2 * FIELD_PADDING, frame.size.height)));
    gr_drawer drawer = {f, draw_field, free_field};
    EX_OK(gr_content_layer_set_drawer(f->content, &drawer));
    EX_OK(gr_layer_set_masks_to_bounds(f->content, true)); /* Scrolled text stops at the padding. */
    EX_OK(gr_layer_add_child(f->box, f->content));
    f->caret = pill(ex, f->box, EX_RECT(0, 0, 1.5, 0), hex(EX_BLUE), 0.75);
    f->placeholder = lay_out(f, (gr_string){placeholder, strlen(placeholder)});
    changed(f);
    return f;
}

bool text_field_focused(const text_field *f)
{
    return f->focused;
}

bool text_field_contains(const text_field *f, gr_point p)
{
    return p.x >= f->host_origin.x && p.y >= f->host_origin.y && p.x < f->host_origin.x + f->frame.size.width &&
           p.y < f->host_origin.y + f->frame.size.height;
}

static void set_focused(text_field *f, bool focused)
{
    if (focused == f->focused)
        return;
    f->focused = focused;
    border(f->box, focused ? 2 : 1, focused ? hex(EX_BLUE) : hex(LINE_COLOR));
    if (focused)
        changed(f);
    else
    {
        f->composition = (gr_text_range){0, 0};
        if (f->ex->host)
            EX_OK(gr_scene_host_set_text_input(f->ex->host, NULL));
        update_caret(f);
        EX_OK(gr_content_layer_set_needs_display(f->content));
    }
}

void text_field_update(text_field *f, double time)
{
    f->now = time;
    bool on = !f->focused || fmod(time - f->blink_start, 1.0) < 0.5;
    if (on == f->caret_on)
        return;
    f->caret_on = on;
    update_caret(f);
}

/* Text offset under a host-space point. */
static size_t offset_at(const text_field *f, gr_point p)
{
    if (!f->layout)
        return f->length;
    gr_point o = layout_origin(f);
    gr_point local = {p.x - f->host_origin.x - FIELD_PADDING - o.x, p.y - f->host_origin.y - o.y};
    return gr_text_layout_hit_test(f->layout, local).index;
}

bool text_field_pointer(text_field *f, const gr_event *e)
{
    const gr_pointer_event *p = &e->value.pointer;
    switch (e->type)
    {
        case GR_EVENT_POINTER_DOWN:
            if (!text_field_contains(f, p->position))
            {
                set_focused(f, false);
                return false;
            }
            set_focused(f, true);
            /* A click ends a composition in progress, keeping its text. */
            f->composition = (gr_text_range){0, 0};
            f->focus = offset_at(f, p->position);
            if (!(e->modifiers & GR_MODIFIER_SHIFT))
                f->anchor = f->focus;
            if (p->click_count >= 2)
                f->anchor = 0, f->focus = f->length;
            f->dragging = p->button == GR_BUTTON_PRIMARY;
            changed(f);
            return true;
        case GR_EVENT_POINTER_MOVE:
            if (!f->dragging)
                return false;
            f->focus = offset_at(f, p->position);
            changed(f);
            return true;
        case GR_EVENT_POINTER_UP:
        case GR_EVENT_POINTER_CANCEL:
        {
            bool was = f->dragging;
            f->dragging = false;
            return was;
        }
        default:
            return false;
    }
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t';
}

static size_t previous_stop(const text_field *f, size_t i)
{
    if (!i)
        return 0;
    if (f->layout)
        return gr_text_layout_previous_caret_stop(f->layout, i);
    do
        --i;
    while (i && ((unsigned char)f->text[i] & 0xC0) == 0x80);
    return i;
}

static size_t next_stop(const text_field *f, size_t i)
{
    if (i >= f->length)
        return f->length;
    if (f->layout)
        return gr_text_layout_next_caret_stop(f->layout, i);
    do
        ++i;
    while (i < f->length && ((unsigned char)f->text[i] & 0xC0) == 0x80);
    return i;
}

static size_t word_start(const text_field *f, size_t i)
{
    while (i && is_space(f->text[i - 1]))
        i = previous_stop(f, i);
    while (i && !is_space(f->text[i - 1]))
        i = previous_stop(f, i);
    return i;
}

static size_t word_end(const text_field *f, size_t i)
{
    while (i < f->length && is_space(f->text[i]))
        i = next_stop(f, i);
    while (i < f->length && !is_space(f->text[i]))
        i = next_stop(f, i);
    return i;
}

/* Replaces range with text and puts the caret after it. */
static void replace(text_field *f, gr_text_range range, gr_string text)
{
    if (f->length - (range.end - range.start) + text.count > FIELD_CAPACITY)
        return;
    memmove(f->text + range.start + text.count, f->text + range.end, f->length - range.end);
    if (text.count)
        memcpy(f->text + range.start, text.data, text.count);
    f->length = f->length - (range.end - range.start) + text.count;
    f->anchor = f->focus = range.start + text.count;
}

/* Deletes the selection, or else the range from the caret to target. */
static void delete_to(text_field *f, size_t target)
{
    gr_text_range sel = selection(f);
    if (sel.start == sel.end)
        sel = (gr_text_range){min_size(sel.start, target), max_size(sel.start, target)};
    replace(f, sel, (gr_string){NULL, 0});
}

static void move_caret(text_field *f, size_t target, bool extend)
{
    f->focus = target;
    if (!extend)
        f->anchor = target;
}

static bool command(text_field *f, gr_text_command_event c)
{
    gr_text_range sel = selection(f);
    bool collapse = !c.extend && sel.start != sel.end;
    switch (c.command)
    {
        case GR_TEXT_COMMAND_DELETE_BACKWARD:
            delete_to(f, previous_stop(f, f->focus));
            break;
        case GR_TEXT_COMMAND_DELETE_FORWARD:
            delete_to(f, next_stop(f, f->focus));
            break;
        case GR_TEXT_COMMAND_DELETE_WORD_BACKWARD:
            delete_to(f, word_start(f, f->focus));
            break;
        case GR_TEXT_COMMAND_DELETE_WORD_FORWARD:
            delete_to(f, word_end(f, f->focus));
            break;
        case GR_TEXT_COMMAND_DELETE_TO_LINE_START:
            delete_to(f, 0);
            break;
        case GR_TEXT_COMMAND_DELETE_TO_LINE_END:
            delete_to(f, f->length);
            break;
        case GR_TEXT_COMMAND_MOVE_LEFT:
            move_caret(f, collapse ? sel.start : previous_stop(f, f->focus), c.extend);
            break;
        case GR_TEXT_COMMAND_MOVE_RIGHT:
            move_caret(f, collapse ? sel.end : next_stop(f, f->focus), c.extend);
            break;
        case GR_TEXT_COMMAND_MOVE_WORD_LEFT:
            move_caret(f, word_start(f, f->focus), c.extend);
            break;
        case GR_TEXT_COMMAND_MOVE_WORD_RIGHT:
            move_caret(f, word_end(f, f->focus), c.extend);
            break;
        case GR_TEXT_COMMAND_MOVE_UP:
        case GR_TEXT_COMMAND_MOVE_LINE_START:
        case GR_TEXT_COMMAND_MOVE_DOCUMENT_START:
        case GR_TEXT_COMMAND_MOVE_PAGE_UP:
            move_caret(f, 0, c.extend);
            break;
        case GR_TEXT_COMMAND_MOVE_DOWN:
        case GR_TEXT_COMMAND_MOVE_LINE_END:
        case GR_TEXT_COMMAND_MOVE_DOCUMENT_END:
        case GR_TEXT_COMMAND_MOVE_PAGE_DOWN:
            move_caret(f, f->length, c.extend);
            break;
        case GR_TEXT_COMMAND_SELECT_ALL:
            f->anchor = 0;
            f->focus = f->length;
            break;
        case GR_TEXT_COMMAND_CANCEL:
            set_focused(f, false);
            return true;
        case GR_TEXT_COMMAND_INSERT_NEWLINE:
            return true; /* A single line has nowhere to break. */
        case GR_TEXT_COMMAND_INSERT_TAB:
        case GR_TEXT_COMMAND_INSERT_BACKTAB:
            return false;
    }
    changed(f);
    return true;
}

bool text_field_event(text_field *f, const gr_event *e)
{
    if (!f->focused)
        return false;
    switch (e->type)
    {
        case GR_EVENT_TEXT_EDIT:
        {
            const gr_text_edit_event *edit = &e->value.edit;
            replace(f, edit->replace, edit->text);
            f->anchor = edit->selection.start;
            f->focus = edit->selection.end;
            f->composition = edit->composition;
            changed(f);
            return true;
        }
        case GR_EVENT_TEXT_COMMAND:
            return command(f, e->value.command);
        case GR_EVENT_KEY_DOWN:
        {
            /* Select all with the platform's shortcut modifier; other keys go on to text input. */
#ifdef __APPLE__
            gr_modifiers shortcut = GR_MODIFIER_SUPER;
#else
            gr_modifiers shortcut = GR_MODIFIER_CONTROL;
#endif
            if (!e->value.key.composing && (e->modifiers & shortcut) && e->value.key.character == 'a')
                return command(f, (gr_text_command_event){GR_TEXT_COMMAND_SELECT_ALL, false});
            return false;
        }
        default:
            return false;
    }
}
