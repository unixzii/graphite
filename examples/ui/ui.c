#include "example.h"

#define LINE 0xD1D1D6
#define FAINT 0xF2F2F4
#define SELECT 0x007AFF

typedef struct
{
    example *ex;
    gr_layer *switch_track, *switch_knob, *slider_fill, *slider_knob, *caret, *spinner, *progress;
} ui_scene;

typedef enum
{
    PLUS,
    MINUS,
    RING,
    DOT,
    SQUARE,
    LINES,
    PLAY,
    STAR,
    DOWNLOAD,
    SEARCH,
    SPEAKER,
    GEAR,
    CHEVRON,
    CHECK_MARK,
    LOCK
} glyph_kind;

typedef struct
{
    glyph_kind kind;
    gr_point point;
    double angle;
} glyph_item;

typedef struct
{
    gr_color color;
    size_t count;
    glyph_item items[];
} glyph_data;

#define ICONS(d, p, r, c, ...)                                                                                         \
    glyphs(d, p, r, c, (glyph_item[]){__VA_ARGS__}, sizeof((glyph_item[]){__VA_ARGS__}) / sizeof(glyph_item))

#define INK alpha(hex(EX_INK), 0.85f)
#define MUTED alpha(hex(EX_INK), 0.32f)

typedef struct
{
    gr_text_layout *layout;
    gr_point origin;
    gr_color color;
} label_data;

/* Room around the layout box for glyph overhang (italics, emoji). */
#define LABEL_PAD 4.0

static void draw_label(void *userdata, gr_canvas *canvas, const gr_draw_info *info)
{
    (void)info;
    const label_data *data = userdata;
    EX_OK(gr_canvas_draw_text_layout(canvas, data->layout, data->origin, data->color));
}

static void free_label(void *userdata)
{
    label_data *data = userdata;
    gr_text_layout_unref(data->layout);
    free(data);
}

/* Shows an owned layout with its box at (x, y). */
static gr_layer *text_layer(ui_scene *d, gr_layer *parent, gr_text_layout *layout, double x, double y, gr_color color)
{
    gr_size size = gr_text_layout_get_metrics(layout).size;
    label_data *data = malloc(sizeof(*data));
    if (!data)
        abort();
    *data = (label_data){layout, {LABEL_PAD, LABEL_PAD}, color};
    gr_layer *l = new_layer(d->ex, GR_LAYER_CONTENT);
    EX_OK(gr_layer_set_frame(
        l, EX_RECT(x - LABEL_PAD, y - LABEL_PAD, size.width + 2 * LABEL_PAD, size.height + 2 * LABEL_PAD)));
    gr_drawer drawer = {data, draw_label, free_label};
    EX_OK(gr_content_layer_set_drawer(l, &drawer));
    EX_OK(gr_layer_add_child(parent, l));
    return l;
}

typedef enum
{
    TONE_INK,
    TONE_MUTED,
    TONE_WHITE
} label_tone;

/* Zero fields mean defaults: 13 pt, weight 400, ink. */
typedef struct
{
    label_tone tone;
    double size;
    double width; /**< > 0 aligns the text inside this width, starting at x. */
    gr_text_align align;
    uint16_t weight;
} label_style;

#define LABEL(...) ((label_style){.tone = TONE_INK, __VA_ARGS__})

static gr_text_layout *layout_text(ui_scene *d, const char *utf8, const gr_text_style *base, gr_text_span_list spans,
                                   const gr_text_layout_options *options)
{
    gr_attributed_text *text = gr_attributed_text_new((gr_string){utf8, strlen(utf8)}, base, spans);
    if (!text)
        abort();
    gr_text_layout *layout = gr_text_system_layout(d->ex->text, text, options);
    if (!layout)
        abort();
    gr_attributed_text_unref(text);
    return layout;
}

/* A single-line label vertically centered on cy. Without a text system it falls back to a placeholder bar. */
static gr_layer *label(ui_scene *d, gr_layer *parent, double x, double cy, const char *utf8, label_style s)
{
    if (!s.size)
        s.size = 13;
    if (!s.weight)
        s.weight = 400;
    gr_color color = s.tone == TONE_WHITE ? EX_WHITE : s.tone == TONE_MUTED ? MUTED : INK;
    if (!d->ex->text)
    {
        size_t characters = 0;
        for (const char *c = utf8; *c; ++c)
            characters += ((unsigned char)*c & 0xC0) != 0x80;
        double w = fmin((double)characters * 0.5 * s.size, s.width > 0 ? s.width - 8 : 400);
        double left = s.width <= 0 || s.align == GR_TEXT_ALIGN_START ? x
                      : s.align == GR_TEXT_ALIGN_CENTER              ? x + (s.width - w) / 2
                                                                     : x + s.width - w;
        return pill(d->ex, parent, EX_RECT(left, cy - 4, w, 8), color, 4);
    }
    gr_text_style base = gr_text_style_default();
    base.size = s.size;
    base.weight = s.weight;
    gr_text_layout_options options = gr_text_layout_options_default();
    options.wrap = GR_TEXT_WRAP_NONE;
    if (s.width > 0)
    {
        options.has_max_width = true;
        options.max_width = s.width;
        options.align = s.align;
        options.truncation = GR_TEXT_TRUNCATE_END;
    }
    gr_text_layout *layout = layout_text(d, utf8, &base, (gr_text_span_list){NULL, 0}, &options);
    double height = gr_text_layout_get_metrics(layout).size.height;
    return text_layer(d, parent, layout, x, cy - height / 2, color);
}

static size_t find(const char *haystack, const char *needle)
{
    const char *at = strstr(haystack, needle);
    if (!at)
        abort();
    return (size_t)(at - haystack);
}

static gr_text_span span_of(const char *text, const char *word, gr_text_style_fields fields)
{
    size_t start = find(text, word);
    return (gr_text_span){{start, start + strlen(word)}, fields, gr_text_style_default()};
}

static const char body[] =
    "Graphite renders this text with the platform text stack: wrapping, bidi, fallback fonts and the ellipsis "
    "at the end. Mixed scripts need no extra work either. "
    "Emoji work as well \xF0\x9F\x8E\xA8\xF0\x9F\x9A\x80\xE2\x9C\xA8\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0"
    "\x9F\x92\xBB\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5\n"
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Nullam fringilla, est ut feugiat ultrices, elit "
    "lacus ultricies nibh, id commodo tortor nisi id elit.\n"
    "春眠不觉晓，处处闻啼鸟。夜来风雨声，花落知多少。\n"
    "Class aptent taciti sociosqu ad litora torquent per conubia nostra, per inceptos himenaeos. Morbi vel elit "
    "erat. Maecenas dignissim, dui et pharetra rutrum, tellus lectus rutrum mi, a convallis libero nisi quis "
    "tellus. Nulla facilisi. Nullam eleifend lobortis nisl, in porttitor tellus malesuada vitae. Aenean lacus "
    "tellus, pellentesque quis molestie quis, fringilla in arcu.";

/* The editor's wrapped, styled paragraph, clipped to max_lines with an ellipsis. */
static void body_text(ui_scene *d, gr_layer *editor)
{
    if (!d->ex->text)
    {
        for (size_t i = 0; i < 10; ++i)
            label(d, editor, 14, 142 + (double)i * 15.5, "Lorem ipsum dolor sit amet, consectetur adipiscing elit",
                  LABEL());
        return;
    }
    gr_text_span spans[4] = {
        span_of(body, "Graphite", GR_TEXT_STYLE_WEIGHT),
        span_of(body, "fallback fonts", GR_TEXT_STYLE_DECORATIONS),
        span_of(body, "Lorem ipsum", GR_TEXT_STYLE_SLANT),
        span_of(body, "春眠不觉晓，处处闻啼鸟。夜来风雨声，花落知多少。", GR_TEXT_STYLE_FAMILY | GR_TEXT_STYLE_COLOR),
    };
    spans[0].style.weight = 700;
    spans[1].style.decorations = GR_TEXT_DECORATION_UNDERLINE;
    spans[2].style.slant = GR_FONT_SLANT_ITALIC;
    spans[3].style.family = (gr_string){"Songti SC", strlen("Songti SC")};
    spans[3].style.color = hex(0x9B30B5);
    gr_text_style base = gr_text_style_default();
    base.size = 12.5;
    gr_text_layout_options options = gr_text_layout_options_default();
    options.has_max_width = true;
    options.max_width = 436;
    options.max_lines = 10;
    options.truncation = GR_TEXT_TRUNCATE_END;
    gr_text_layout *layout = layout_text(d, body, &base, (gr_text_span_list){spans, 4}, &options);
    /* Line heights differ per platform (Pango's are taller); keep the paragraph above the status bar. */
    const double room = 158;
    gr_text_layout_metrics m = gr_text_layout_get_metrics(layout);
    if (m.size.height > room)
    {
        options.max_lines = (uint32_t)floor(room / (m.size.height / (double)m.line_count));
        gr_text_layout_unref(layout);
        layout = layout_text(d, body, &base, (gr_text_span_list){spans, 4}, &options);
    }
    text_layer(d, editor, layout, 14, 134, INK);
}

static gr_layer *panel(ui_scene *d, gr_layer *parent, gr_rect rect)
{
    gr_layer *p = pill(d->ex, parent, rect, EX_WHITE, 10);
    border(p, 1, hex(LINE));
    EX_OK(gr_layer_set_masks_to_bounds(p, true));
    return p;
}

static gr_layer *hairline(ui_scene *d, gr_layer *parent, gr_rect rect)
{
    gr_layer *l = layer(d->ex, parent, rect);
    background(l, hex(LINE));
    return l;
}

static gr_layer *button(ui_scene *d, gr_layer *parent, gr_rect rect)
{
    gr_layer *l = pill(d->ex, parent, rect, EX_WHITE, 6);
    border(l, 1, hex(LINE));
    return l;
}

static void icon_line(gr_canvas *cx, gr_point c, gr_point a, gr_point b, gr_stroke *pen)
{
    gr_path_builder *p = gr_path_builder_new();
    move_to(p, EX_POINT(c.x + a.x, c.y + a.y));
    line_to(p, EX_POINT(c.x + b.x, c.y + b.y));
    draw_stroke(cx, finish_path(p), gr_stroke_ref(pen));
}

static void draw_glyph(gr_canvas *cx, glyph_item item, gr_color color)
{
    gr_point c = item.point;
    gr_stroke *pen = stroke(paint(color), 1.6, GR_CAP_ROUND, GR_JOIN_ROUND, (gr_double_list){0}, 0);
    gr_path_builder *p;
#define SEG(x, y, u, v) icon_line(cx, c, EX_POINT(x, y), EX_POINT(u, v), pen)
    switch (item.kind)
    {
        case PLUS:
            SEG(-6, 0, 6, 0);
            SEG(0, -6, 0, 6);
            break;
        case MINUS:
            SEG(-6, 0, 6, 0);
            break;
        case RING:
            draw_stroke(cx, circle(c, 6), gr_stroke_ref(pen));
            break;
        case DOT:
            draw_fill(cx, circle(c, 5), GR_FILL_NONZERO, paint(color));
            break;
        case SQUARE:
            draw_stroke(cx, rounded(EX_RECT(c.x - 6, c.y - 6, 12, 12), 3, GR_CORNERS_ALL), gr_stroke_ref(pen));
            break;
        case LINES:
            for (int y = -5; y <= 5; y += 5)
                SEG(-6, y, 6, y);
            break;
        case PLAY:
            p = gr_path_builder_new();
            move_to(p, EX_POINT(c.x - 4, c.y - 6));
            line_to(p, EX_POINT(c.x + 6, c.y));
            line_to(p, EX_POINT(c.x - 4, c.y + 6));
            close_path(p);
            draw_fill(cx, finish_path(p), GR_FILL_NONZERO, paint(color));
            break;
        case STAR:
            draw_fill(cx, star_path(c, 7, 3.2, 5, 0), GR_FILL_NONZERO, paint(color));
            break;
        case DOWNLOAD:
            SEG(0, -7, 0, 3);
            SEG(-4, -1, 0, 3);
            SEG(4, -1, 0, 3);
            SEG(-6, 7, 6, 7);
            break;
        case SEARCH:
            draw_stroke(cx, circle(EX_POINT(c.x - 1.5, c.y - 1.5), 5), gr_stroke_ref(pen));
            SEG(2, 2, 6.5, 6.5);
            break;
        case SPEAKER:
            p = gr_path_builder_new();
            move_to(p, EX_POINT(c.x - 7, c.y - 3));
            line_to(p, EX_POINT(c.x - 3, c.y - 3));
            line_to(p, EX_POINT(c.x + 2, c.y - 7));
            line_to(p, EX_POINT(c.x + 2, c.y + 7));
            line_to(p, EX_POINT(c.x - 3, c.y + 3));
            line_to(p, EX_POINT(c.x - 7, c.y + 3));
            close_path(p);
            draw_fill(cx, finish_path(p), GR_FILL_NONZERO, paint(color));
            SEG(5, -3, 5, 3);
            break;
        case GEAR:
            draw_stroke(cx, circle(c, 5), gr_stroke_ref(pen));
            for (size_t i = 0; i < 6; ++i)
            {
                double a = (double)i * EX_TAU / 6;
                SEG(5 * cos(a), 5 * sin(a), 7.5 * cos(a), 7.5 * sin(a));
            }
            break;
        case CHEVRON:
        {
            double sn = sin(item.angle), co = cos(item.angle);
            gr_point a = {-2.5 * co + 5 * sn, -2.5 * sn - 5 * co}, b = {2.5 * co, 2.5 * sn},
                     end = {-2.5 * co - 5 * sn, -2.5 * sn + 5 * co};
            icon_line(cx, c, a, b, pen);
            icon_line(cx, c, b, end, pen);
            break;
        }
        case CHECK_MARK:
            SEG(-6, 0, -2, 4);
            SEG(-2, 4, 6, -5);
            break;
        case LOCK:
            draw_fill(cx, rounded(EX_RECT(c.x - 6, c.y - 1, 12, 9), 2, GR_CORNERS_ALL), GR_FILL_NONZERO, paint(color));
            draw_stroke(cx, rounded(EX_RECT(c.x - 4, c.y - 8, 8, 10), 4, GR_CORNER_TOP_LEFT | GR_CORNER_TOP_RIGHT),
                        gr_stroke_ref(pen));
            break;
    }
#undef SEG
    gr_stroke_unref(pen);
}

static void draw_icons(void *userdata, gr_canvas *cx, const gr_draw_info *info)
{
    (void)info;
    const glyph_data *data = userdata;
    for (size_t i = 0; i < data->count; ++i)
        draw_glyph(cx, data->items[i], data->color);
}

static gr_layer *glyphs(ui_scene *d, gr_layer *parent, gr_rect rect, gr_color color, const glyph_item *items,
                        size_t count)
{
    glyph_data *data = malloc(sizeof(*data) + count * sizeof(*items));
    if (!data)
        abort();
    data->color = color;
    data->count = count;
    memcpy(data->items, items, count * sizeof(*items));
    gr_layer *l = new_layer(d->ex, GR_LAYER_CONTENT);
    EX_OK(gr_layer_set_frame(l, rect));
    gr_drawer drawer = {data, draw_icons, free};
    EX_OK(gr_content_layer_set_drawer(l, &drawer));
    EX_OK(gr_layer_add_child(parent, l));
    return l;
}

static gr_color mix(gr_color a, gr_color b, double t)
{
    float v = (float)t;
    return (gr_color){a.r + (b.r - a.r) * v, a.g + (b.g - a.g) * v, a.b + (b.b - a.b) * v, a.a + (b.a - a.a) * v};
}

static void *build(example *ex)
{
    ui_scene *d = calloc(1, sizeof(*d));
    if (!d)
        abort();
    d->ex = ex;
    gr_layer *root = gr_scene_get_root(d->ex->scene);
    background(root, hex(0xFAFAFA));
    gr_color ink = alpha(hex(EX_INK), 0.85f);
    /* Header. */
    gr_layer *header = layer(d->ex, root, EX_RECT(0, 0, 1000, 44));
    background(header, hex(0xEBEBED));
    hairline(d, root, EX_RECT(0, 44, 1000, 1));
    gr_layer *segments = pill(d->ex, header, EX_RECT(356, 8, 288, 28), EX_WHITE, 7);
    border(segments, 1, hex(0xC8C8CC));
    EX_OK(gr_layer_set_masks_to_bounds(segments, true));
    background(layer(d->ex, segments, EX_RECT(96, 0, 96, 28)), hex(0xD9D9DD));
    const char *pages[] = {"Page 1", "Page 2", "Page 3"};
    for (size_t i = 0; i < 3; ++i)
        label(d, segments, (double)i * 96, 14, pages[i], LABEL(.width = 96, .align = GR_TEXT_ALIGN_CENTER));
    gr_layer *menu = button(d, header, EX_RECT(954, 8, 34, 28));
    ICONS(d, menu, EX_RECT(0, 0, 34, 28), ink, {LINES, {17, 14}, 0});
    /* Left column. */
    for (size_t i = 0; i < 2; ++i)
    {
        gr_layer *spin = panel(d, root, EX_RECT(12 + (double)i * 52, 56, 44, 120));
        hairline(d, spin, EX_RECT(0, 40, 44, 1));
        hairline(d, spin, EX_RECT(0, 80, 44, 1));
        label(d, spin, 0, 60, "50", LABEL(.width = 44, .align = GR_TEXT_ALIGN_CENTER));
        ICONS(d, spin, EX_RECT(0, 0, 44, 120), ink, {PLUS, {22, 20}, 0}, {MINUS, {22, 100}, 0});
        if (i)
            EX_OK(gr_layer_set_opacity(spin, 0.4f));
    }
    ICONS(d, root, EX_RECT(116, 56, 120, 120), ink, {SPEAKER, {24, 32}, 0}, {RING, {24, 82}, 0});
    gr_layer *list = panel(d, root, EX_RECT(12, 188, 224, 198));
    gr_layer *rows = layer(d->ex, list, EX_RECT(0, 0, 224, 152));
    EX_OK(gr_layer_set_masks_to_bounds(rows, true));
    label(d, rows, 12, 14, "* Translation updates:", LABEL());
    const char *languages[] = {"Aragonese", "Basque", "Brazilian Portuguese", "Dutch", "German", "Hebrew",
                               "Spanish",   "Swedish"};
    for (size_t i = 0; i < 8; ++i)
        label(d, rows, 18, 32 + (double)i * 17, languages[i], LABEL());
    hairline(d, list, EX_RECT(0, 152, 224, 1));
    gr_layer *toggles = layer(d->ex, list, EX_RECT(76, 160, 72, 30));
    border(toggles, 1, hex(LINE));
    EX_OK(gr_layer_set_corner_radius(toggles, 6));
    EX_OK(gr_layer_set_masks_to_bounds(toggles, true));
    background(layer(d->ex, toggles, EX_RECT(0, 0, 36, 30)), hex(0xE5E5EA));
    ICONS(d, toggles, EX_RECT(0, 0, 72, 30), ink, {RING, {18, 15}, 0}, {SQUARE, {54, 15}, 0});
    gr_layer *gear = button(d, list, EX_RECT(180, 160, 32, 30));
    ICONS(d, gear, EX_RECT(0, 0, 32, 30), ink, {GEAR, {16, 15}, 0});
    gr_layer *settings = panel(d, root, EX_RECT(12, 398, 224, 190));
    const char *settings_rows[] = {"Wi-Fi", "Volume", "Sync", "Count", "Privacy"};
    for (size_t i = 0; i < 5; ++i)
    {
        double y = (double)i * 38;
        if (i)
            hairline(d, settings, EX_RECT(0, y, 224, 1));
        label(d, settings, 14, y + 19, settings_rows[i], LABEL());
    }
    d->switch_track = pill(d->ex, settings, EX_RECT(164, 9, 44, 24), hex(0xE0E0E4), 12);
    d->switch_knob = pill(d->ex, settings, EX_RECT(166, 11, 20, 20), EX_WHITE, 10);
    drop_shadow(d->switch_knob, alpha(EX_BLACK, 0.25f), 0, 1, 1, NULL);
    pill(d->ex, settings, EX_RECT(96, 56, 112, 3), hex(0xE0E0E4), 1.5);
    d->slider_fill = pill(d->ex, settings, EX_RECT(96, 56, 60, 3), hex(SELECT), 1.5);
    d->slider_knob = pill(d->ex, settings, EX_RECT(0, 0, 18, 18), EX_WHITE, 9);
    border(d->slider_knob, 1, hex(LINE));
    drop_shadow(d->slider_knob, alpha(EX_BLACK, 0.2f), 0, 1, 1, NULL);
    gr_layer *spin = button(d, settings, EX_RECT(116, 119, 96, 28));
    hairline(d, spin, EX_RECT(40, 0, 1, 28));
    hairline(d, spin, EX_RECT(68, 0, 1, 28));
    label(d, spin, 0, 14, "54", LABEL(.width = 40, .align = GR_TEXT_ALIGN_CENTER));
    gr_layer *lock = button(d, settings, EX_RECT(180, 157, 32, 28));
    ICONS(d, lock, EX_RECT(0, 0, 32, 28), ink, {LOCK, {16, 15}, 0});
    ICONS(d, settings, EX_RECT(0, 0, 224, 190), ink, {CHECK_MARK, {200, 95}, 0}, {MINUS, {170, 133}, 0},
          {PLUS, {198, 133}, 0});
    /* Center column. */
    gr_layer *editor = panel(d, root, EX_RECT(248, 56, 464, 330));
    const char *menus[] = {"File", "Edit", "View", "Help"};
    for (size_t i = 0; i < 4; ++i)
        label(d, editor, 14 + (double)i * 52, 18, menus[i], LABEL());
    ICONS(d, editor, EX_RECT(0, 36, 220, 40), ink, {SQUARE, {24, 20}, 0}, {DOWNLOAD, {60, 20}, 0},
          {SEARCH, {110, 20}, 0}, {LINES, {146, 20}, 0});
    hairline(d, editor, EX_RECT(86, 46, 1, 20));
    pill(d->ex, editor, EX_RECT(180, 45, 40, 22), hex(0x9B30B5), 3);
    background(layer(d->ex, editor, EX_RECT(0, 80, 464, 46)), hex(FAINT));
    gr_layer *search = pill(d->ex, editor, EX_RECT(150, 88, 164, 30), EX_WHITE, 7);
    border(search, 2, alpha(hex(SELECT), 0.8f));
    ICONS(d, search, EX_RECT(0, 0, 30, 30), ink, {SEARCH, {15, 15}, 0});
    gr_layer *query = label(d, search, 30, 15, "Graphite text", LABEL());
    gr_rect typed = gr_layer_get_frame(query);
    double caret_x = typed.origin.x + typed.size.width + (d->ex->text ? 1 - LABEL_PAD : 2);
    d->caret = pill(d->ex, search, EX_RECT(caret_x, 7, 1.5, 16), hex(EX_INK), 0);
    body_text(d, editor);
    hairline(d, editor, EX_RECT(0, 296, 464, 1));
    background(layer(d->ex, editor, EX_RECT(0, 297, 464, 33)), hex(FAINT));
    label(d, editor, 14, 313, "All systems are operating normally", LABEL(.size = 12));
    pill(d->ex, editor, EX_RECT(300, 310, 150, 6), hex(0xE0E0E4), 3);
    d->progress = pill(d->ex, editor, EX_RECT(300, 310, 0, 6), hex(SELECT), 3);
    gr_layer *left_right = panel(d, root, EX_RECT(248, 398, 228, 190));
    hairline(d, left_right, EX_RECT(114, 0, 1, 190));
    label(d, left_right, 0, 95, "Left", LABEL(.width = 114, .align = GR_TEXT_ALIGN_CENTER));
    label(d, left_right, 114, 95, "Right", LABEL(.width = 114, .align = GR_TEXT_ALIGN_CENTER));
    gr_layer *top_bottom = panel(d, root, EX_RECT(488, 398, 224, 190));
    hairline(d, top_bottom, EX_RECT(0, 95, 224, 1));
    label(d, top_bottom, 0, 48, "Top", LABEL(.width = 224, .align = GR_TEXT_ALIGN_CENTER));
    label(d, top_bottom, 0, 142, "Bottom", LABEL(.width = 224, .align = GR_TEXT_ALIGN_CENTER));
    d->spinner = shape(d->ex, top_bottom, EX_RECT(186, 154, 24, 24));
    set_path(d->spinner, circle(EX_POINT(12, 12), 9));
    shape_stroke(d->spinner,
                 stroke(paint(alpha(hex(EX_INK), 0.7f)), 2.5, GR_CAP_ROUND, GR_JOIN_MITER, (gr_double_list){0}, 0));
    EX_OK(gr_shape_layer_set_stroke_end(d->spinner, 0.7));
    /* Right column. */
    gr_layer *calendar = panel(d, root, EX_RECT(724, 56, 264, 204));
    label(d, calendar, 26, 20, "November", LABEL(.width = 78, .align = GR_TEXT_ALIGN_CENTER));
    label(d, calendar, 166, 20, "2026", LABEL(.width = 72, .align = GR_TEXT_ALIGN_CENTER));
    const char *weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    for (size_t i = 0; i < 7; ++i)
        label(d, calendar, 12 + (double)i * 36, 46, weekdays[i],
              LABEL(.width = 32, .align = GR_TEXT_ALIGN_CENTER, .weight = 600));
    for (size_t row = 0; row < 6; ++row)
        for (size_t col = 0; col < 7; ++col)
        {
            double x = 12 + (double)col * 36, y = 66 + (double)row * 22;
            size_t n = row * 7 + col;
            /* November 2026 starts on a Sunday, after a row of October, and has 30 days. */
            int day = n < 7 ? 25 + (int)n : n < 37 ? (int)n - 6 : (int)n - 36;
            char number[4];
            snprintf(number, sizeof(number), "%d", day);
            label_style style = LABEL(.size = 12, .width = 32, .align = GR_TEXT_ALIGN_CENTER);
            if (n == 13)
            {
                pill(d->ex, calendar, EX_RECT(x + 2, y - 4, 28, 18), hex(SELECT), 4);
                style.tone = TONE_WHITE;
                style.weight = 600;
            }
            else if (n < 7 || n >= 37)
                style.tone = TONE_MUTED;
            label(d, calendar, x, y + 5, number, style);
        }
    ICONS(d, calendar, EX_RECT(0, 0, 264, 40), ink, {CHEVRON, {18, 20}, EX_PI}, {CHEVRON, {112, 20}, 0},
          {CHEVRON, {158, 20}, EX_PI}, {CHEVRON, {246, 20}, 0});
    ICONS(d, root, EX_RECT(724, 266, 20, 20), ink, {CHEVRON, {10, 10}, EX_PI / 2});
    label(d, root, 744, 276, "Expander", LABEL());
    gr_layer *icons = panel(d, root, EX_RECT(724, 292, 264, 112));
    glyph_kind kinds[] = {RING, SPEAKER, DOT, SQUARE, LINES, PLAY, STAR, DOWNLOAD, SEARCH, GEAR, PLUS, LOCK};
    glyph_item items[21];
    for (size_t row = 0; row < 3; ++row)
        for (size_t col = 0; col < 7; ++col)
            items[row * 7 + col] =
                (glyph_item){kinds[(row * 7 + col * 5) % 12], {22 + (double)col * 36.5, 20 + (double)row * 36}, 0};
    glyphs(d, icons, EX_RECT(0, 0, 264, 112), ink, items, 21);
    gr_layer *group = pill(d->ex, root, EX_RECT(724, 414, 108, 30), EX_WHITE, 6);
    border(group, 1, hex(LINE));
    EX_OK(gr_layer_set_masks_to_bounds(group, true));
    hairline(d, group, EX_RECT(36, 0, 1, 30));
    hairline(d, group, EX_RECT(72, 0, 1, 30));
    ICONS(d, group, EX_RECT(0, 0, 108, 30), ink, {MINUS, {18, 15}, 0}, {PLUS, {54, 15}, 0}, {DOT, {90, 15}, 0});
    const char *actions[] = {"Inform", "Act", "Configure", "Select"};
    for (size_t i = 0; i < 4; ++i)
    {
        gr_layer *b = button(d, root, EX_RECT(724, 454 + (double)i * 34, 264, 28));
        label(d, b, 0, 14, actions[i], LABEL(.width = 264, .align = GR_TEXT_ALIGN_CENTER));
    }
    return d;
}

static void update(void *state, double t)
{
    ui_scene *d = state;
    bool on = (size_t)(t / 3) % 2 == 1;
    double slide = smooth(fmod(t, 3) / 0.25), p = on ? slide : 1 - slide;
    if (t < 3)
        p = 0;
    EX_OK(gr_layer_set_position(d->switch_knob, EX_POINT(176 + p * 20, 21)));
    background(d->switch_track, mix(hex(0xE0E0E4), hex(SELECT), p));
    double value = 0.2 + 0.6 * smooth(wave(t, 9, 0)), x = 96 + value * 112;
    EX_OK(gr_layer_set_bounds(d->slider_fill, EX_RECT(0, 0, x - 96, 3)));
    EX_OK(gr_layer_set_position(d->slider_fill, EX_POINT(96 + (x - 96) / 2, 57.5)));
    EX_OK(gr_layer_set_position(d->slider_knob, EX_POINT(x, 57.5)));
    EX_OK(gr_layer_set_hidden(d->caret, (size_t)(t * 2) % 2 == 1));
    EX_OK(gr_layer_set_transform(d->spinner, rotation(t * EX_TAU)));
    double progress = fmod(t, 5) / 5 * 150;
    EX_OK(gr_layer_set_bounds(d->progress, EX_RECT(0, 0, progress, 6)));
    EX_OK(gr_layer_set_position(d->progress, EX_POINT(300 + progress / 2, 313)));
}

int main(int argc, char **argv)
{
    static const example_desc desc = {"ui", {1000, 600}, build, update, free};
    return example_main(argc, argv, &desc);
}
