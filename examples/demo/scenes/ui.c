#include "demo.h"

#define LINE 0xD1D1D6
#define FAINT 0xF2F2F4
#define SELECT 0xE95420

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
    CLOSE,
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

static gr_layer *text(demo *d, gr_layer *parent, double x, double y, double width)
{
    return pill(d, parent, D_RECT(x, y, width, 8), alpha(hex(0x3A3A3C), 0.85f), 4);
}

static gr_layer *muted_text(demo *d, gr_layer *parent, double x, double y, double width)
{
    return pill(d, parent, D_RECT(x, y, width, 8), alpha(hex(0x3A3A3C), 0.3f), 4);
}

static gr_layer *panel(demo *d, gr_layer *parent, gr_rect rect)
{
    gr_layer *p = pill(d, parent, rect, D_WHITE, 10);
    border(p, 1, hex(LINE));
    D_OK(gr_layer_set_masks_to_bounds(p, true));
    return p;
}

static gr_layer *hairline(demo *d, gr_layer *parent, gr_rect rect)
{
    gr_layer *l = layer(d, parent, rect);
    background(l, hex(LINE));
    return l;
}

static gr_layer *button(demo *d, gr_layer *parent, gr_rect rect)
{
    gr_layer *l = pill(d, parent, rect, D_WHITE, 6);
    border(l, 1, hex(LINE));
    return l;
}

static void icon_line(gr_canvas *cx, gr_point c, gr_point a, gr_point b, gr_stroke *pen)
{
    gr_path_builder *p = gr_path_builder_new();
    move_to(p, D_POINT(c.x + a.x, c.y + a.y));
    line_to(p, D_POINT(c.x + b.x, c.y + b.y));
    draw_stroke(cx, finish_path(p), gr_stroke_ref(pen));
}

static void draw_glyph(gr_canvas *cx, glyph_item item, gr_color color)
{
    gr_point c = item.point;
    gr_stroke *pen = stroke(paint(color), 1.6, GR_CAP_ROUND, GR_JOIN_ROUND, (gr_double_list){0}, 0);
    gr_path_builder *p;
#define SEG(x, y, u, v) icon_line(cx, c, D_POINT(x, y), D_POINT(u, v), pen)
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
            draw_stroke(cx, rounded(D_RECT(c.x - 6, c.y - 6, 12, 12), 3, GR_CORNERS_ALL), gr_stroke_ref(pen));
            break;
        case LINES:
            for (int y = -5; y <= 5; y += 5)
                SEG(-6, y, 6, y);
            break;
        case PLAY:
            p = gr_path_builder_new();
            move_to(p, D_POINT(c.x - 4, c.y - 6));
            line_to(p, D_POINT(c.x + 6, c.y));
            line_to(p, D_POINT(c.x - 4, c.y + 6));
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
            draw_stroke(cx, circle(D_POINT(c.x - 1.5, c.y - 1.5), 5), gr_stroke_ref(pen));
            SEG(2, 2, 6.5, 6.5);
            break;
        case SPEAKER:
            p = gr_path_builder_new();
            move_to(p, D_POINT(c.x - 7, c.y - 3));
            line_to(p, D_POINT(c.x - 3, c.y - 3));
            line_to(p, D_POINT(c.x + 2, c.y - 7));
            line_to(p, D_POINT(c.x + 2, c.y + 7));
            line_to(p, D_POINT(c.x - 3, c.y + 3));
            line_to(p, D_POINT(c.x - 7, c.y + 3));
            close_path(p);
            draw_fill(cx, finish_path(p), GR_FILL_NONZERO, paint(color));
            SEG(5, -3, 5, 3);
            break;
        case GEAR:
            draw_stroke(cx, circle(c, 5), gr_stroke_ref(pen));
            for (size_t i = 0; i < 6; ++i)
            {
                double a = (double)i * D_TAU / 6;
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
        case CLOSE:
            SEG(-4, -4, 4, 4);
            SEG(4, -4, -4, 4);
            break;
        case LOCK:
            draw_fill(cx, rounded(D_RECT(c.x - 6, c.y - 1, 12, 9), 2, GR_CORNERS_ALL), GR_FILL_NONZERO, paint(color));
            draw_stroke(cx, rounded(D_RECT(c.x - 4, c.y - 8, 8, 10), 4, GR_CORNER_TOP_LEFT | GR_CORNER_TOP_RIGHT),
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

static gr_layer *glyphs(demo *d, gr_layer *parent, gr_rect rect, gr_color color, const glyph_item *items, size_t count)
{
    glyph_data *data = malloc(sizeof(*data) + count * sizeof(*items));
    if (!data)
        abort();
    data->color = color;
    data->count = count;
    memcpy(data->items, items, count * sizeof(*items));
    gr_layer *l = new_layer(d, GR_LAYER_CONTENT);
    D_OK(gr_layer_set_frame(l, rect));
    gr_drawer drawer = {data, draw_icons, free};
    D_OK(gr_content_layer_set_drawer(l, &drawer));
    D_OK(gr_layer_add_child(parent, l));
    return l;
}

static double width(size_t i, double min, double max)
{
    double x = sin(((double)i + 1) * 12.9898) * 43758.545;
    return min + (x - floor(x)) * (max - min);
}

static gr_color mix(gr_color a, gr_color b, double t)
{
    float v = (float)t;
    return (gr_color){a.r + (b.r - a.r) * v, a.g + (b.g - a.g) * v, a.b + (b.b - a.b) * v, a.a + (b.a - a.a) * v};
}

void ui_build(demo *d)
{
    gr_layer *root = gr_scene_get_root(d->scene);
    background(root, hex(0xFAFAFA));
    gr_color ink = alpha(hex(D_INK), 0.85f);
    /* Header. */
    gr_layer *header = layer(d, root, D_RECT(0, 0, 1000, 44));
    background(header, hex(0xEBEBED));
    hairline(d, root, D_RECT(0, 44, 1000, 1));
    gr_layer *segments = pill(d, header, D_RECT(356, 8, 288, 28), D_WHITE, 7);
    border(segments, 1, hex(0xC8C8CC));
    D_OK(gr_layer_set_masks_to_bounds(segments, true));
    background(layer(d, segments, D_RECT(96, 0, 96, 28)), hex(0xD9D9DD));
    for (size_t i = 0; i < 3; ++i)
        text(d, segments, (double)i * 96 + 28, 10, 40);
    gr_layer *menu = button(d, header, D_RECT(864, 8, 34, 28));
    ICONS(d, menu, D_RECT(0, 0, 34, 28), ink, {LINES, {17, 14}, 0});
    glyph_kind controls[] = {MINUS, SQUARE, CLOSE};
    for (size_t i = 0; i < 3; ++i)
    {
        gr_layer *c = pill(d, header, D_RECT(910 + (double)i * 30, 11, 22, 22), hex(0xDCDCE0), 11);
        ICONS(d, c, D_RECT(0, 0, 22, 22), ink, {controls[i], {11, 11}, 0});
    }
    /* Left column. */
    for (size_t i = 0; i < 2; ++i)
    {
        gr_layer *spin = panel(d, root, D_RECT(12 + (double)i * 52, 56, 44, 120));
        hairline(d, spin, D_RECT(0, 40, 44, 1));
        hairline(d, spin, D_RECT(0, 80, 44, 1));
        text(d, spin, 13, 56, 18);
        ICONS(d, spin, D_RECT(0, 0, 44, 120), ink, {PLUS, {22, 20}, 0}, {MINUS, {22, 100}, 0});
        if (i)
            D_OK(gr_layer_set_opacity(spin, 0.4f));
    }
    ICONS(d, root, D_RECT(116, 56, 120, 120), ink, {SPEAKER, {24, 32}, 0}, {RING, {24, 82}, 0});
    gr_layer *list = panel(d, root, D_RECT(12, 188, 224, 198));
    gr_layer *rows = layer(d, list, D_RECT(0, 0, 224, 152));
    D_OK(gr_layer_set_masks_to_bounds(rows, true));
    text(d, rows, 12, 10, 150);
    for (size_t i = 0; i < 8; ++i)
        text(d, rows, 18, 28 + (double)i * 17, width(i, 50, 140));
    hairline(d, list, D_RECT(0, 152, 224, 1));
    gr_layer *toggles = layer(d, list, D_RECT(76, 160, 72, 30));
    border(toggles, 1, hex(LINE));
    D_OK(gr_layer_set_corner_radius(toggles, 6));
    D_OK(gr_layer_set_masks_to_bounds(toggles, true));
    background(layer(d, toggles, D_RECT(0, 0, 36, 30)), hex(0xE5E5EA));
    ICONS(d, toggles, D_RECT(0, 0, 72, 30), ink, {RING, {18, 15}, 0}, {SQUARE, {54, 15}, 0});
    gr_layer *gear = button(d, list, D_RECT(180, 160, 32, 30));
    ICONS(d, gear, D_RECT(0, 0, 32, 30), ink, {GEAR, {16, 15}, 0});
    gr_layer *settings = panel(d, root, D_RECT(12, 398, 224, 190));
    for (size_t i = 0; i < 5; ++i)
    {
        double y = (double)i * 38;
        if (i)
            hairline(d, settings, D_RECT(0, y, 224, 1));
        text(d, settings, 14, y + 15, 38);
    }
    d->switch_track = pill(d, settings, D_RECT(164, 9, 44, 24), hex(0xE0E0E4), 12);
    d->switch_knob = pill(d, settings, D_RECT(166, 11, 20, 20), D_WHITE, 10);
    demo_shadow(d->switch_knob, alpha(D_BLACK, 0.25f), 0, 1, 1, NULL);
    pill(d, settings, D_RECT(96, 56, 112, 3), hex(0xE0E0E4), 1.5);
    d->slider_fill = pill(d, settings, D_RECT(96, 56, 60, 3), hex(SELECT), 1.5);
    d->slider_knob = pill(d, settings, D_RECT(0, 0, 18, 18), D_WHITE, 9);
    border(d->slider_knob, 1, hex(LINE));
    demo_shadow(d->slider_knob, alpha(D_BLACK, 0.2f), 0, 1, 1, NULL);
    gr_layer *spin = button(d, settings, D_RECT(116, 119, 96, 28));
    hairline(d, spin, D_RECT(40, 0, 1, 28));
    hairline(d, spin, D_RECT(68, 0, 1, 28));
    text(d, spin, 10, 10, 16);
    gr_layer *lock = button(d, settings, D_RECT(180, 157, 32, 28));
    ICONS(d, lock, D_RECT(0, 0, 32, 28), ink, {LOCK, {16, 15}, 0});
    ICONS(d, settings, D_RECT(0, 0, 224, 190), ink, {CHECK_MARK, {200, 95}, 0}, {MINUS, {170, 133}, 0},
          {PLUS, {198, 133}, 0});
    /* Center column. */
    gr_layer *editor = panel(d, root, D_RECT(248, 56, 464, 330));
    double widths[] = {26, 26, 30, 28};
    for (size_t i = 0; i < 4; ++i)
        text(d, editor, 14 + (double)i * 52, 14, widths[i]);
    ICONS(d, editor, D_RECT(0, 36, 220, 40), ink, {SQUARE, {24, 20}, 0}, {DOWNLOAD, {60, 20}, 0},
          {SEARCH, {110, 20}, 0}, {LINES, {146, 20}, 0});
    hairline(d, editor, D_RECT(86, 46, 1, 20));
    pill(d, editor, D_RECT(180, 45, 40, 22), hex(0x9B30B5), 3);
    background(layer(d, editor, D_RECT(0, 80, 464, 46)), hex(FAINT));
    gr_layer *search = pill(d, editor, D_RECT(150, 88, 164, 30), D_WHITE, 7);
    border(search, 2, alpha(hex(SELECT), 0.8f));
    ICONS(d, search, D_RECT(0, 0, 30, 30), ink, {SEARCH, {15, 15}, 0});
    d->caret = pill(d, search, D_RECT(30, 7, 1.5, 16), hex(D_INK), 0);
    for (size_t i = 0; i < 10; ++i)
        text(d, editor, 14, 138 + (double)i * 15.5, width(i + 20, 180, 430));
    hairline(d, editor, D_RECT(0, 296, 464, 1));
    background(layer(d, editor, D_RECT(0, 297, 464, 33)), hex(FAINT));
    text(d, editor, 14, 309, 170);
    pill(d, editor, D_RECT(300, 310, 150, 6), hex(0xE0E0E4), 3);
    d->progress = pill(d, editor, D_RECT(300, 310, 0, 6), hex(SELECT), 3);
    gr_layer *left_right = panel(d, root, D_RECT(248, 398, 228, 190));
    hairline(d, left_right, D_RECT(114, 0, 1, 190));
    text(d, left_right, 42, 91, 30);
    text(d, left_right, 154, 91, 34);
    gr_layer *top_bottom = panel(d, root, D_RECT(488, 398, 224, 190));
    hairline(d, top_bottom, D_RECT(0, 95, 224, 1));
    text(d, top_bottom, 96, 44, 28);
    text(d, top_bottom, 90, 138, 40);
    d->spinner = shape(d, top_bottom, D_RECT(186, 154, 24, 24));
    set_path(d->spinner, circle(D_POINT(12, 12), 9));
    shape_stroke(d->spinner,
                 stroke(paint(alpha(hex(D_INK), 0.7f)), 2.5, GR_CAP_ROUND, GR_JOIN_MITER, (gr_double_list){0}, 0));
    D_OK(gr_shape_layer_set_stroke_end(d->spinner, 0.7));
    /* Right column. */
    gr_layer *calendar = panel(d, root, D_RECT(724, 56, 264, 204));
    text(d, calendar, 34, 16, 62);
    text(d, calendar, 176, 16, 34);
    for (size_t i = 0; i < 7; ++i)
        text(d, calendar, 18 + (double)i * 36, 42, 22);
    for (size_t row = 0; row < 6; ++row)
        for (size_t col = 0; col < 7; ++col)
        {
            double x = 12 + (double)col * 36, y = 66 + (double)row * 22;
            size_t n = row * 7 + col;
            if (n == 11)
            {
                pill(d, calendar, D_RECT(x + 2, y - 4, 28, 18), hex(SELECT), 4);
                pill(d, calendar, D_RECT(x + 10, y + 1, 12, 8), D_WHITE, 4);
            }
            else if (n < 6 || n >= 36)
                muted_text(d, calendar, x + 9, y + 1, 14);
            else
                text(d, calendar, x + 9, y + 1, n - 5 < 10 ? 8 : 14);
        }
    ICONS(d, calendar, D_RECT(0, 0, 264, 40), ink, {CHEVRON, {18, 20}, D_PI}, {CHEVRON, {112, 20}, 0},
          {CHEVRON, {158, 20}, D_PI}, {CHEVRON, {246, 20}, 0});
    ICONS(d, root, D_RECT(724, 266, 20, 20), ink, {CHEVRON, {10, 10}, D_PI / 2});
    text(d, root, 744, 272, 56);
    gr_layer *icons = panel(d, root, D_RECT(724, 292, 264, 112));
    glyph_kind kinds[] = {RING, SPEAKER, DOT, SQUARE, LINES, PLAY, STAR, DOWNLOAD, SEARCH, GEAR, PLUS, LOCK};
    glyph_item items[21];
    for (size_t row = 0; row < 3; ++row)
        for (size_t col = 0; col < 7; ++col)
            items[row * 7 + col] =
                (glyph_item){kinds[(row * 7 + col * 5) % 12], {22 + (double)col * 36.5, 20 + (double)row * 36}, 0};
    glyphs(d, icons, D_RECT(0, 0, 264, 112), ink, items, 21);
    gr_layer *group = pill(d, root, D_RECT(724, 414, 108, 30), D_WHITE, 6);
    border(group, 1, hex(LINE));
    D_OK(gr_layer_set_masks_to_bounds(group, true));
    hairline(d, group, D_RECT(36, 0, 1, 30));
    hairline(d, group, D_RECT(72, 0, 1, 30));
    ICONS(d, group, D_RECT(0, 0, 108, 30), ink, {MINUS, {18, 15}, 0}, {PLUS, {54, 15}, 0}, {DOT, {90, 15}, 0});
    double labels[] = {44, 30, 62, 42};
    for (size_t i = 0; i < 4; ++i)
    {
        gr_layer *b = button(d, root, D_RECT(724, 454 + (double)i * 34, 264, 28));
        text(d, b, 132 - labels[i] / 2, 10, labels[i]);
    }
}

void ui_update(demo *d, double t)
{
    bool on = (size_t)(t / 3) % 2 == 1;
    double slide = smooth(fmod(t, 3) / 0.25), p = on ? slide : 1 - slide;
    if (t < 3)
        p = 0;
    D_OK(gr_layer_set_position(d->switch_knob, D_POINT(176 + p * 20, 21)));
    background(d->switch_track, mix(hex(0xE0E0E4), hex(SELECT), p));
    double value = 0.2 + 0.6 * smooth(wave(t, 9, 0)), x = 96 + value * 112;
    D_OK(gr_layer_set_bounds(d->slider_fill, D_RECT(0, 0, x - 96, 3)));
    D_OK(gr_layer_set_position(d->slider_fill, D_POINT(96 + (x - 96) / 2, 57.5)));
    D_OK(gr_layer_set_position(d->slider_knob, D_POINT(x, 57.5)));
    D_OK(gr_layer_set_hidden(d->caret, (size_t)(t * 2) % 2 == 1));
    D_OK(gr_layer_set_transform(d->spinner, rotation(t * D_TAU)));
    double progress = fmod(t, 5) / 5 * 150;
    D_OK(gr_layer_set_bounds(d->progress, D_RECT(0, 0, progress, 6)));
    D_OK(gr_layer_set_position(d->progress, D_POINT(300 + progress / 2, 313)));
}
