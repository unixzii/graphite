#include "example.h"
#include "field.h"

#include <stdarg.h>

/* Pointer pad, scroll list and key strip that show what the host reports. */

#define PAD EX_RECT(24, 24, 540, 404)
#define LIST EX_RECT(588, 24, 288, 404)
#define FIELD EX_RECT(24, 452, 852, 52)
#define STRIP EX_RECT(24, 520, 852, 84)
#define ROWS 30
#define ROW_HEIGHT 40.0
#define TILES 6
#define RIPPLES 8
#define RIPPLE_TIME 0.6
#define INK alpha(hex(EX_INK), 0.85f)
#define MUTED alpha(hex(EX_INK), 0.45f)
#define LINE 0xD1D1D6

static const char *const code_names[] = {
    [GR_KEY_CODE_UNKNOWN] = "Unidentified",
    [GR_KEY_CODE_A] = "KeyA",
    [GR_KEY_CODE_B] = "KeyB",
    [GR_KEY_CODE_C] = "KeyC",
    [GR_KEY_CODE_D] = "KeyD",
    [GR_KEY_CODE_E] = "KeyE",
    [GR_KEY_CODE_F] = "KeyF",
    [GR_KEY_CODE_G] = "KeyG",
    [GR_KEY_CODE_H] = "KeyH",
    [GR_KEY_CODE_I] = "KeyI",
    [GR_KEY_CODE_J] = "KeyJ",
    [GR_KEY_CODE_K] = "KeyK",
    [GR_KEY_CODE_L] = "KeyL",
    [GR_KEY_CODE_M] = "KeyM",
    [GR_KEY_CODE_N] = "KeyN",
    [GR_KEY_CODE_O] = "KeyO",
    [GR_KEY_CODE_P] = "KeyP",
    [GR_KEY_CODE_Q] = "KeyQ",
    [GR_KEY_CODE_R] = "KeyR",
    [GR_KEY_CODE_S] = "KeyS",
    [GR_KEY_CODE_T] = "KeyT",
    [GR_KEY_CODE_U] = "KeyU",
    [GR_KEY_CODE_V] = "KeyV",
    [GR_KEY_CODE_W] = "KeyW",
    [GR_KEY_CODE_X] = "KeyX",
    [GR_KEY_CODE_Y] = "KeyY",
    [GR_KEY_CODE_Z] = "KeyZ",
    [GR_KEY_CODE_DIGIT_0] = "Digit0",
    [GR_KEY_CODE_DIGIT_1] = "Digit1",
    [GR_KEY_CODE_DIGIT_2] = "Digit2",
    [GR_KEY_CODE_DIGIT_3] = "Digit3",
    [GR_KEY_CODE_DIGIT_4] = "Digit4",
    [GR_KEY_CODE_DIGIT_5] = "Digit5",
    [GR_KEY_CODE_DIGIT_6] = "Digit6",
    [GR_KEY_CODE_DIGIT_7] = "Digit7",
    [GR_KEY_CODE_DIGIT_8] = "Digit8",
    [GR_KEY_CODE_DIGIT_9] = "Digit9",
    [GR_KEY_CODE_MINUS] = "Minus",
    [GR_KEY_CODE_EQUAL] = "Equal",
    [GR_KEY_CODE_BRACKET_LEFT] = "BracketLeft",
    [GR_KEY_CODE_BRACKET_RIGHT] = "BracketRight",
    [GR_KEY_CODE_BACKSLASH] = "Backslash",
    [GR_KEY_CODE_SEMICOLON] = "Semicolon",
    [GR_KEY_CODE_QUOTE] = "Quote",
    [GR_KEY_CODE_BACKQUOTE] = "Backquote",
    [GR_KEY_CODE_COMMA] = "Comma",
    [GR_KEY_CODE_PERIOD] = "Period",
    [GR_KEY_CODE_SLASH] = "Slash",
    [GR_KEY_CODE_INTL_BACKSLASH] = "IntlBackslash",
    [GR_KEY_CODE_INTL_RO] = "IntlRo",
    [GR_KEY_CODE_INTL_YEN] = "IntlYen",
    [GR_KEY_CODE_ENTER] = "Enter",
    [GR_KEY_CODE_ESCAPE] = "Escape",
    [GR_KEY_CODE_BACKSPACE] = "Backspace",
    [GR_KEY_CODE_TAB] = "Tab",
    [GR_KEY_CODE_SPACE] = "Space",
    [GR_KEY_CODE_CAPS_LOCK] = "CapsLock",
    [GR_KEY_CODE_SHIFT_LEFT] = "ShiftLeft",
    [GR_KEY_CODE_SHIFT_RIGHT] = "ShiftRight",
    [GR_KEY_CODE_CONTROL_LEFT] = "ControlLeft",
    [GR_KEY_CODE_CONTROL_RIGHT] = "ControlRight",
    [GR_KEY_CODE_ALT_LEFT] = "AltLeft",
    [GR_KEY_CODE_ALT_RIGHT] = "AltRight",
    [GR_KEY_CODE_META_LEFT] = "MetaLeft",
    [GR_KEY_CODE_META_RIGHT] = "MetaRight",
    [GR_KEY_CODE_CONTEXT_MENU] = "ContextMenu",
    [GR_KEY_CODE_FN] = "Fn",
    [GR_KEY_CODE_ARROW_LEFT] = "ArrowLeft",
    [GR_KEY_CODE_ARROW_RIGHT] = "ArrowRight",
    [GR_KEY_CODE_ARROW_UP] = "ArrowUp",
    [GR_KEY_CODE_ARROW_DOWN] = "ArrowDown",
    [GR_KEY_CODE_INSERT] = "Insert",
    [GR_KEY_CODE_DELETE] = "Delete",
    [GR_KEY_CODE_HOME] = "Home",
    [GR_KEY_CODE_END] = "End",
    [GR_KEY_CODE_PAGE_UP] = "PageUp",
    [GR_KEY_CODE_PAGE_DOWN] = "PageDown",
    [GR_KEY_CODE_PRINT_SCREEN] = "PrintScreen",
    [GR_KEY_CODE_SCROLL_LOCK] = "ScrollLock",
    [GR_KEY_CODE_PAUSE] = "Pause",
    [GR_KEY_CODE_NUM_LOCK] = "NumLock",
    [GR_KEY_CODE_NUMPAD_0] = "Numpad0",
    [GR_KEY_CODE_NUMPAD_1] = "Numpad1",
    [GR_KEY_CODE_NUMPAD_2] = "Numpad2",
    [GR_KEY_CODE_NUMPAD_3] = "Numpad3",
    [GR_KEY_CODE_NUMPAD_4] = "Numpad4",
    [GR_KEY_CODE_NUMPAD_5] = "Numpad5",
    [GR_KEY_CODE_NUMPAD_6] = "Numpad6",
    [GR_KEY_CODE_NUMPAD_7] = "Numpad7",
    [GR_KEY_CODE_NUMPAD_8] = "Numpad8",
    [GR_KEY_CODE_NUMPAD_9] = "Numpad9",
    [GR_KEY_CODE_NUMPAD_ADD] = "NumpadAdd",
    [GR_KEY_CODE_NUMPAD_SUBTRACT] = "NumpadSubtract",
    [GR_KEY_CODE_NUMPAD_MULTIPLY] = "NumpadMultiply",
    [GR_KEY_CODE_NUMPAD_DIVIDE] = "NumpadDivide",
    [GR_KEY_CODE_NUMPAD_DECIMAL] = "NumpadDecimal",
    [GR_KEY_CODE_NUMPAD_COMMA] = "NumpadComma",
    [GR_KEY_CODE_NUMPAD_EQUAL] = "NumpadEqual",
    [GR_KEY_CODE_NUMPAD_ENTER] = "NumpadEnter",
    [GR_KEY_CODE_F1] = "F1",
    [GR_KEY_CODE_F2] = "F2",
    [GR_KEY_CODE_F3] = "F3",
    [GR_KEY_CODE_F4] = "F4",
    [GR_KEY_CODE_F5] = "F5",
    [GR_KEY_CODE_F6] = "F6",
    [GR_KEY_CODE_F7] = "F7",
    [GR_KEY_CODE_F8] = "F8",
    [GR_KEY_CODE_F9] = "F9",
    [GR_KEY_CODE_F10] = "F10",
    [GR_KEY_CODE_F11] = "F11",
    [GR_KEY_CODE_F12] = "F12",
    [GR_KEY_CODE_F13] = "F13",
    [GR_KEY_CODE_F14] = "F14",
    [GR_KEY_CODE_F15] = "F15",
    [GR_KEY_CODE_F16] = "F16",
    [GR_KEY_CODE_F17] = "F17",
    [GR_KEY_CODE_F18] = "F18",
    [GR_KEY_CODE_F19] = "F19",
    [GR_KEY_CODE_F20] = "F20",
    [GR_KEY_CODE_F21] = "F21",
    [GR_KEY_CODE_F22] = "F22",
    [GR_KEY_CODE_F23] = "F23",
    [GR_KEY_CODE_F24] = "F24",
    [GR_KEY_CODE_LANG_1] = "Lang1",
    [GR_KEY_CODE_LANG_2] = "Lang2",
    [GR_KEY_CODE_KANA_MODE] = "KanaMode",
    [GR_KEY_CODE_CONVERT] = "Convert",
    [GR_KEY_CODE_NON_CONVERT] = "NonConvert",
    [GR_KEY_CODE_AUDIO_VOLUME_MUTE] = "AudioVolumeMute",
    [GR_KEY_CODE_AUDIO_VOLUME_DOWN] = "AudioVolumeDown",
    [GR_KEY_CODE_AUDIO_VOLUME_UP] = "AudioVolumeUp",
};

static const char *const key_names[] = {
    [GR_KEY_UNKNOWN] = "Unidentified",
    [GR_KEY_CHARACTER] = "",
    [GR_KEY_PROCESS] = "Process",
    [GR_KEY_ENTER] = "Enter",
    [GR_KEY_TAB] = "Tab",
    [GR_KEY_BACKSPACE] = "Backspace",
    [GR_KEY_ESCAPE] = "Escape",
    [GR_KEY_DELETE] = "Delete",
    [GR_KEY_INSERT] = "Insert",
    [GR_KEY_HOME] = "Home",
    [GR_KEY_END] = "End",
    [GR_KEY_PAGE_UP] = "PageUp",
    [GR_KEY_PAGE_DOWN] = "PageDown",
    [GR_KEY_ARROW_LEFT] = "ArrowLeft",
    [GR_KEY_ARROW_RIGHT] = "ArrowRight",
    [GR_KEY_ARROW_UP] = "ArrowUp",
    [GR_KEY_ARROW_DOWN] = "ArrowDown",
    [GR_KEY_SHIFT] = "Shift",
    [GR_KEY_CONTROL] = "Control",
    [GR_KEY_ALT] = "Alt",
    [GR_KEY_SUPER] = "Super",
    [GR_KEY_CAPS_LOCK] = "CapsLock",
    [GR_KEY_NUM_LOCK] = "NumLock",
    [GR_KEY_SCROLL_LOCK] = "ScrollLock",
    [GR_KEY_FN] = "Fn",
    [GR_KEY_CONTEXT_MENU] = "ContextMenu",
    [GR_KEY_PRINT_SCREEN] = "PrintScreen",
    [GR_KEY_PAUSE] = "Pause",
};

static const char *const button_names[] = {"none", "primary", "secondary", "middle", "back", "forward"};
static const uint32_t button_colors[] = {EX_INK, EX_BLUE, EX_ORANGE, EX_GREEN, EX_PURPLE, EX_PINK};

static const struct
{
    const char *label;
    gr_cursor cursor;
} tiles[TILES] = {
    {"Pointer", GR_CURSOR_POINTER}, {"Text", GR_CURSOR_TEXT},        {"Crosshair", GR_CURSOR_CROSSHAIR},
    {"Grab", GR_CURSOR_GRAB},       {"Resize", GR_CURSOR_RESIZE_EW}, {"Denied", GR_CURSOR_NOT_ALLOWED},
};

static const struct
{
    const char *label;
    gr_modifiers modifier;
} chips[] = {
    {"Shift", GR_MODIFIER_SHIFT}, {"Ctrl", GR_MODIFIER_CONTROL},   {"Alt", GR_MODIFIER_ALT},
    {"Super", GR_MODIFIER_SUPER}, {"Caps", GR_MODIFIER_CAPS_LOCK},
};

#define CHIPS (sizeof(chips) / sizeof(chips[0]))

/* A single line of text that can change; it lays itself out on each draw. */
typedef struct
{
    gr_text_system *text;
    gr_layer *layer;
    char value[128];
    double size;
    uint16_t weight;
    gr_color color;
    gr_text_align align;
} text_cell;

static void draw_cell(void *userdata, gr_canvas *canvas, const gr_draw_info *info)
{
    const text_cell *t = userdata;
    if (!t->text || !t->value[0])
        return;
    gr_text_style base = gr_text_style_default();
    base.size = t->size;
    base.weight = t->weight;
    gr_attributed_text *text =
        gr_attributed_text_new((gr_string){t->value, strlen(t->value)}, &base, (gr_text_span_list){NULL, 0});
    gr_text_layout_options options = gr_text_layout_options_default();
    options.wrap = GR_TEXT_WRAP_NONE;
    options.has_max_width = true;
    options.max_width = info->bounds.size.width;
    options.align = t->align;
    options.truncation = GR_TEXT_TRUNCATE_END;
    gr_text_layout *layout = gr_text_system_layout(t->text, text, &options);
    gr_attributed_text_unref(text);
    if (!layout)
        return;
    double height = gr_text_layout_get_metrics(layout).size.height;
    EX_OK(gr_canvas_draw_text_layout(canvas, layout, EX_POINT(0, (info->bounds.size.height - height) / 2), t->color));
    gr_text_layout_unref(layout);
}

static text_cell *cell(example *ex, gr_layer *parent, gr_rect rect, double size, uint16_t weight, gr_color color,
                       gr_text_align align, const char *value)
{
    text_cell *t = calloc(1, sizeof(*t));
    if (!t)
        abort();
    *t = (text_cell){.text = ex->text, .size = size, .weight = weight, .color = color, .align = align};
    snprintf(t->value, sizeof(t->value), "%s", value);
    t->layer = new_layer(ex, GR_LAYER_CONTENT);
    EX_OK(gr_layer_set_frame(t->layer, rect));
    gr_drawer drawer = {t, draw_cell, free};
    EX_OK(gr_content_layer_set_drawer(t->layer, &drawer));
    EX_OK(gr_layer_add_child(parent, t->layer));
    return t;
}

static void set_cell(text_cell *t, const char *format, ...)
{
    char value[sizeof(t->value)];
    va_list args;
    va_start(args, format);
    vsnprintf(value, sizeof(value), format, args);
    va_end(args);
    if (strcmp(value, t->value) == 0)
        return;
    memcpy(t->value, value, sizeof(value));
    EX_OK(gr_content_layer_set_needs_display(t->layer));
}

static void set_cell_color(text_cell *t, gr_color color)
{
    t->color = color;
    EX_OK(gr_content_layer_set_needs_display(t->layer));
}

typedef struct
{
    example *ex;
    double now;
    gr_layer *pad, *dot, *hline, *vline, *list_content, *scrollbar;
    gr_layer *ripples[RIPPLES], *tile_layers[TILES], *chip_layers[CHIPS];
    double ripple_start[RIPPLES];
    size_t next_ripple;
    text_cell *status, *key, *detail, *chip_text[CHIPS];
    text_field *field;
    double scroll;
    uint32_t buttons;
    gr_cursor cursor;
    gr_modifiers modifiers;
    bool chips_shown;
} input_scene;

static bool inside(gr_rect r, gr_point p)
{
    return p.x >= r.origin.x && p.y >= r.origin.y && p.x < r.origin.x + r.size.width &&
           p.y < r.origin.y + r.size.height;
}

static gr_rect tile_rect(size_t i)
{
    return EX_RECT(24 + (double)i * 83, PAD.size.height - 56, 76, 40);
}

static void *build(example *ex)
{
    input_scene *d = calloc(1, sizeof(*d));
    if (!d)
        abort();
    d->ex = ex;
    for (size_t i = 0; i < RIPPLES; ++i)
        d->ripple_start[i] = -1;
    gr_layer *root = gr_scene_get_root(ex->scene);
    background(root, hex(0xFAFAFA));

    /* Pointer pad. */
    d->pad = pill(ex, root, PAD, EX_WHITE, 12);
    border(d->pad, 1, hex(LINE));
    EX_OK(gr_layer_set_masks_to_bounds(d->pad, true));
    d->status =
        cell(ex, d->pad, EX_RECT(16, 10, 508, 22), 13, 400, MUTED, GR_TEXT_ALIGN_START, "Move, press, scroll and type");
    d->hline = layer(ex, d->pad, EX_RECT(0, 0, PAD.size.width, 1));
    d->vline = layer(ex, d->pad, EX_RECT(0, 0, 1, PAD.size.height));
    background(d->hline, alpha(hex(EX_BLUE), 0.25f));
    background(d->vline, alpha(hex(EX_BLUE), 0.25f));
    for (size_t i = 0; i < RIPPLES; ++i)
    {
        d->ripples[i] = shape(ex, d->pad, EX_RECT(0, 0, 60, 60));
        set_path(d->ripples[i], circle(EX_POINT(30, 30), 28));
        EX_OK(gr_layer_set_hidden(d->ripples[i], true));
    }
    d->dot = pill(ex, d->pad, EX_RECT(0, 0, 16, 16), alpha(hex(EX_INK), 0.3f), 8);
    EX_OK(gr_layer_set_hidden(d->dot, true));
    EX_OK(gr_layer_set_hidden(d->hline, true));
    EX_OK(gr_layer_set_hidden(d->vline, true));
    for (size_t i = 0; i < TILES; ++i)
    {
        d->tile_layers[i] = pill(ex, d->pad, tile_rect(i), hex(0xF2F2F4), 8);
        border(d->tile_layers[i], 1, hex(LINE));
        gr_rect r = tile_rect(i);
        cell(ex, d->tile_layers[i], EX_RECT(4, 0, r.size.width - 8, r.size.height), 12, 500, INK, GR_TEXT_ALIGN_CENTER,
             tiles[i].label);
    }

    /* Scroll list. */
    gr_layer *list = pill(ex, root, LIST, EX_WHITE, 12);
    border(list, 1, hex(LINE));
    EX_OK(gr_layer_set_masks_to_bounds(list, true));
    d->list_content = layer(ex, list, EX_RECT(0, 0, LIST.size.width, ROWS * ROW_HEIGHT));
    for (size_t i = 0; i < ROWS; ++i)
    {
        char label[16];
        snprintf(label, sizeof(label), "Row %zu", i + 1);
        cell(ex, d->list_content, EX_RECT(16, (double)i * ROW_HEIGHT, 200, ROW_HEIGHT), 13, 400, INK,
             GR_TEXT_ALIGN_START, label);
        if (i)
            background(layer(ex, d->list_content, EX_RECT(0, (double)i * ROW_HEIGHT, LIST.size.width, 1)), hex(LINE));
    }
    double thumb = LIST.size.height * LIST.size.height / (ROWS * ROW_HEIGHT);
    d->scrollbar = pill(ex, list, EX_RECT(LIST.size.width - 8, 4, 4, thumb), alpha(hex(EX_INK), 0.25f), 2);

    d->field = text_field_new(ex, root, FIELD, FIELD.origin,
                              "Click here and type; try an input method, or hold a letter for accents");

    /* Key strip. */
    gr_layer *strip = pill(ex, root, STRIP, EX_WHITE, 12);
    border(strip, 1, hex(LINE));
    d->key = cell(ex, strip, EX_RECT(20, 8, 480, 40), 24, 600, INK, GR_TEXT_ALIGN_START, "No key yet");
    d->detail = cell(ex, strip, EX_RECT(20, 48, 480, 24), 13, 400, MUTED, GR_TEXT_ALIGN_START, "");
    for (size_t i = 0; i < CHIPS; ++i)
    {
        gr_rect r = EX_RECT(STRIP.size.width - 20 - (double)(CHIPS - i) * 64 + 8, 28, 56, 28);
        d->chip_layers[i] = pill(ex, strip, r, hex(0xF2F2F4), 14);
        d->chip_text[i] = cell(ex, d->chip_layers[i], EX_RECT(0, 0, r.size.width, r.size.height), 12, 500, MUTED,
                               GR_TEXT_ALIGN_CENTER, chips[i].label);
    }
    return d;
}

static void update(void *state, double t)
{
    input_scene *d = state;
    d->now = t;
    for (size_t i = 0; i < RIPPLES; ++i)
    {
        if (d->ripple_start[i] < 0)
            continue;
        double p = (t - d->ripple_start[i]) / RIPPLE_TIME;
        if (p >= 1 || p < 0)
        {
            d->ripple_start[i] = -1;
            EX_OK(gr_layer_set_hidden(d->ripples[i], true));
            continue;
        }
        EX_OK(gr_layer_set_transform(d->ripples[i], scaling(0.3 + smooth(p) * 1.2)));
        EX_OK(gr_layer_set_opacity(d->ripples[i], (float)(1 - p)));
    }
}

static void set_cursor(input_scene *d, gr_cursor cursor)
{
    if (cursor == d->cursor || !d->ex->host)
        return;
    d->cursor = cursor;
    gr_scene_host_set_cursor(d->ex->host, cursor);
}

static void show_modifiers(input_scene *d, gr_modifiers modifiers)
{
    if (modifiers == d->modifiers && d->chips_shown)
        return;
    d->modifiers = modifiers;
    d->chips_shown = true;
    for (size_t i = 0; i < CHIPS; ++i)
    {
        bool on = modifiers & chips[i].modifier;
        background(d->chip_layers[i], on ? hex(EX_BLUE) : hex(0xF2F2F4));
        set_cell_color(d->chip_text[i], on ? EX_WHITE : MUTED);
    }
}

static void spawn_ripple(input_scene *d, gr_point at, gr_button button)
{
    size_t i = d->next_ripple++ % RIPPLES;
    gr_layer *r = d->ripples[i];
    shape_stroke(r, stroke(paint(hex(button_colors[button])), 2, GR_CAP_ROUND, GR_JOIN_ROUND, (gr_double_list){0}, 0));
    EX_OK(gr_layer_set_position(r, at));
    EX_OK(gr_layer_set_transform(r, scaling(0.3)));
    EX_OK(gr_layer_set_opacity(r, 1));
    EX_OK(gr_layer_set_hidden(r, false));
    d->ripple_start[i] = d->now;
}

/* Index of the tile under p (pad coordinates), or -1. */
static int tile_at(gr_point p)
{
    for (size_t i = 0; i < TILES; ++i)
        if (inside(tile_rect(i), p))
            return (int)i;
    return -1;
}

static bool pointer(input_scene *d, const gr_event *e)
{
    const gr_pointer_event *p = &e->value.pointer;
    if (text_field_pointer(d->field, e))
    {
        set_cursor(d, GR_CURSOR_TEXT);
        return true;
    }
    bool gone = e->type == GR_EVENT_POINTER_LEAVE || e->type == GR_EVENT_POINTER_CANCEL;
    d->buttons = gone ? 0 : p->buttons;
    gr_point local = {p->position.x - PAD.origin.x, p->position.y - PAD.origin.y};
    bool in_pad = !gone && (inside(PAD, p->position) || d->buttons);
    EX_OK(gr_layer_set_hidden(d->dot, !in_pad));
    EX_OK(gr_layer_set_hidden(d->hline, !in_pad));
    EX_OK(gr_layer_set_hidden(d->vline, !in_pad));
    if (in_pad)
    {
        EX_OK(gr_layer_set_position(d->dot, local));
        EX_OK(gr_layer_set_position(d->hline, EX_POINT(PAD.size.width / 2, local.y)));
        EX_OK(gr_layer_set_position(d->vline, EX_POINT(local.x, PAD.size.height / 2)));
        gr_button held = GR_BUTTON_NONE;
        for (int b = GR_BUTTON_FORWARD; b > GR_BUTTON_NONE; --b)
            if (d->buttons & 1u << b)
                held = (gr_button)b;
        background(d->dot, held ? hex(button_colors[held]) : alpha(hex(EX_INK), 0.3f));
        EX_OK(gr_layer_set_transform(d->dot, scaling(held ? 1.5 : 1)));
    }
    if (e->type == GR_EVENT_POINTER_DOWN && inside(PAD, p->position))
        spawn_ripple(d, local, p->button);

    /* Cursor tiles: hovering shows the cursor; pressing the grab tile grabs. */
    int tile = inside(PAD, p->position) && !gone ? tile_at(local) : -1;
    gr_cursor cursor = tile < 0 ? GR_CURSOR_DEFAULT : tiles[tile].cursor;
    if (!gone && text_field_contains(d->field, p->position))
        cursor = GR_CURSOR_TEXT;
    if (cursor == GR_CURSOR_GRAB && d->buttons)
        cursor = GR_CURSOR_GRABBING;
    set_cursor(d, cursor);
    for (size_t i = 0; i < TILES; ++i)
        background(d->tile_layers[i], (int)i == tile ? hex(0xE5E5EA) : hex(0xF2F2F4));

    static const char *const types[] = {
        [GR_EVENT_POINTER_DOWN] = "down",   [GR_EVENT_POINTER_UP] = "up",       [GR_EVENT_POINTER_MOVE] = "move",
        [GR_EVENT_POINTER_ENTER] = "enter", [GR_EVENT_POINTER_LEAVE] = "leave", [GR_EVENT_POINTER_CANCEL] = "cancel"};
    static const char *const kinds[] = {"mouse", "pen", "touch"};
    if (e->type == GR_EVENT_POINTER_DOWN || e->type == GR_EVENT_POINTER_UP)
        set_cell(d->status, "%s %s  x %.0f  y %.0f  clicks %u  %s", button_names[p->button], types[e->type],
                 p->position.x, p->position.y, p->click_count, kinds[p->kind]);
    else
        set_cell(d->status, "%s  x %.0f  y %.0f  buttons 0x%x  %s", types[e->type], p->position.x, p->position.y,
                 p->buttons, kinds[p->kind]);
    return true;
}

static bool scroll(input_scene *d, const gr_event *e)
{
    const gr_scroll_event *s = &e->value.scroll;
    if (!inside(LIST, s->position))
        return false;
    double range = ROWS * ROW_HEIGHT - LIST.size.height;
    double step = s->unit == GR_SCROLL_UNIT_LINE ? ROW_HEIGHT : 1;
    d->scroll = fmin(fmax(d->scroll + s->delta.y * step, 0), range);
    EX_OK(gr_layer_set_position(d->list_content, EX_POINT(LIST.size.width / 2, ROWS * ROW_HEIGHT / 2 - d->scroll)));
    double thumb = LIST.size.height * LIST.size.height / (ROWS * ROW_HEIGHT);
    EX_OK(gr_layer_set_position(
        d->scrollbar,
        EX_POINT(LIST.size.width - 6, 4 + thumb / 2 + d->scroll / range * (LIST.size.height - thumb - 8))));
    static const char *const phases[] = {"", " begin", " update", " end"};
    set_cell(d->status, "scroll %+.2f, %+.2f %s%s%s", s->delta.x, s->delta.y,
             s->unit == GR_SCROLL_UNIT_LINE ? "lines" : "points", phases[s->phase], s->momentum ? " momentum" : "");
    return true;
}

/* Encodes a code point as UTF-8 into out (at least 5 bytes). */
static void utf8(uint32_t c, char *out)
{
    if (c < 0x80)
        *out++ = (char)c;
    else if (c < 0x800)
    {
        *out++ = (char)(0xC0 | c >> 6);
        *out++ = (char)(0x80 | (c & 0x3F));
    }
    else if (c < 0x10000)
    {
        *out++ = (char)(0xE0 | c >> 12);
        *out++ = (char)(0x80 | (c >> 6 & 0x3F));
        *out++ = (char)(0x80 | (c & 0x3F));
    }
    else
    {
        *out++ = (char)(0xF0 | c >> 18);
        *out++ = (char)(0x80 | (c >> 12 & 0x3F));
        *out++ = (char)(0x80 | (c >> 6 & 0x3F));
        *out++ = (char)(0x80 | (c & 0x3F));
    }
    *out = 0;
}

static bool key(input_scene *d, const gr_event *e)
{
    const gr_key_event *k = &e->value.key;
    char text[8];
    if (k->key == GR_KEY_CHARACTER)
    {
        text[0] = '\'';
        utf8(k->character, text + 1);
        strcat(text, "'");
    }
    if (k->key >= GR_KEY_F1)
        snprintf(text, sizeof(text), "F%d", (int)(k->key - GR_KEY_F1) + 1);
    set_cell(d->key, "%s", k->key == GR_KEY_CHARACTER || k->key >= GR_KEY_F1 ? text : key_names[k->key]);
    set_cell(d->detail, "%s  %s%s  native 0x%x", code_names[k->code], e->type == GR_EVENT_KEY_DOWN ? "down" : "up",
             k->repeat ? " repeat" : "", k->native_code);
    /* A focused field takes its shortcuts and leaves the rest to text input. */
    if (text_field_focused(d->field))
        return text_field_event(d->field, e);
    /* Leave shortcuts with Alt or Super to the platform, such as Alt+F4 and Command+Q. */
    return !(e->modifiers & (GR_MODIFIER_ALT | GR_MODIFIER_SUPER));
}

static bool input(void *state, const gr_event *e)
{
    input_scene *d = state;
    show_modifiers(d, e->modifiers);
    switch (e->type)
    {
        case GR_EVENT_POINTER_DOWN:
        case GR_EVENT_POINTER_UP:
        case GR_EVENT_POINTER_MOVE:
        case GR_EVENT_POINTER_ENTER:
        case GR_EVENT_POINTER_LEAVE:
        case GR_EVENT_POINTER_CANCEL:
            return pointer(d, e);
        case GR_EVENT_SCROLL:
            return scroll(d, e);
        case GR_EVENT_KEY_DOWN:
        case GR_EVENT_KEY_UP:
            return key(d, e);
        case GR_EVENT_TEXT_EDIT:
        {
            const gr_text_edit_event *t = &e->value.edit;
            set_cell(d->status, "edit %zu-%zu \"%.*s\"  selection %zu-%zu  composition %zu-%zu", t->replace.start,
                     t->replace.end, (int)t->text.count, t->text.data ? t->text.data : "", t->selection.start,
                     t->selection.end, t->composition.start, t->composition.end);
            return text_field_event(d->field, e);
        }
        case GR_EVENT_TEXT_COMMAND:
        {
            static const char *const names[] = {
                "insert newline",
                "insert tab",
                "insert backtab",
                "delete backward",
                "delete forward",
                "delete word backward",
                "delete word forward",
                "delete to line start",
                "delete to line end",
                "move left",
                "move right",
                "move up",
                "move down",
                "move word left",
                "move word right",
                "move line start",
                "move line end",
                "move document start",
                "move document end",
                "move page up",
                "move page down",
                "select all",
                "cancel",
            };
            set_cell(d->status, "command %s%s", names[e->value.command.command],
                     e->value.command.extend ? " (extend)" : "");
            return text_field_event(d->field, e);
        }
        case GR_EVENT_TIMER:
            return text_field_event(d->field, e);
        default:
            return false;
    }
}

int main(int argc, char **argv)
{
    static const example_desc desc = {"input", {900, 624}, build, update, free, NULL, input, NULL};
    return example_main(argc, argv, &desc);
}
