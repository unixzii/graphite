/* Shared pieces: springs, text labels, path icons and buttons. */
#include "mines.h"

#include <stdarg.h>

/* Springs */

void spring_to(spring *s, double target)
{
    s->target = target;
    s->active = s->value != target || s->velocity != 0;
}

void spring_set(spring *s, double value, double target)
{
    s->value = value;
    s->velocity = 0;
    spring_to(s, target);
}

void spring_kick(spring *s, double velocity)
{
    s->velocity += velocity;
    s->active = true;
}

void spring_step(spring *s, spring_params p, double dt)
{
    if (!s->active)
        return;
    double w = EX_TAU / p.response;
    s->velocity += (-w * w * (s->value - s->target) - 2 * w * p.damping * s->velocity) * dt;
    s->value += s->velocity * dt;
    if (fabs(s->value - s->target) < 1e-3 && fabs(s->velocity) < 1e-2)
    {
        s->value = s->target;
        s->velocity = 0;
        s->active = false;
    }
}

void spring_run(spring *s, spring_params p, int steps)
{
    for (int i = 0; i < steps && s->active; ++i)
        spring_step(s, p, STEP);
}

gr_color mix(gr_color a, gr_color b, double t)
{
    float u = (float)clampd(t, 0, 1);
    return (gr_color){a.r + (b.r - a.r) * u, a.g + (b.g - a.g) * u, a.b + (b.b - a.b) * u, a.a + (b.a - a.a) * u};
}

double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

gr_rect inset(gr_rect r, double d)
{
    return EX_RECT(r.origin.x + d, r.origin.y + d, fmax(0, r.size.width - 2 * d), fmax(0, r.size.height - 2 * d));
}

bool inside(gr_rect r, gr_point p)
{
    return p.x >= r.origin.x && p.y >= r.origin.y && p.x < r.origin.x + r.size.width &&
           p.y < r.origin.y + r.size.height;
}

double ease_out(double t)
{
    t = clampd(t, 0, 1);
    return 1 - pow(1 - t, 3);
}

/* Text */

static gr_text_layout *layout_text(gr_text_system *text, const char *value, double size, uint16_t weight,
                                   double spacing, double max_width, gr_text_align align)
{
    gr_text_style base = gr_text_style_default();
    base.size = size;
    base.weight = weight;
    base.letter_spacing = spacing;
    gr_attributed_text *t =
        gr_attributed_text_new((gr_string){value, strlen(value)}, &base, (gr_text_span_list){NULL, 0});
    gr_text_layout_options options = gr_text_layout_options_default();
    options.wrap = GR_TEXT_WRAP_NONE;
    if (max_width > 0)
    {
        options.has_max_width = true;
        options.max_width = max_width;
        options.truncation = GR_TEXT_TRUNCATE_END;
    }
    options.align = align;
    gr_text_layout *layout = gr_text_system_layout(text, t, &options);
    gr_attributed_text_unref(t);
    return layout;
}

/* Without a text system, one rounded dot per character keeps the layout readable. */
static void draw_placeholder(gr_canvas *c, const char *value, gr_rect box, double size, gr_color color,
                             gr_text_align align)
{
    size_t n = strlen(value);
    double dot = size * 0.32, pitch = size * 0.55, width = (double)n * pitch;
    double x = align == GR_TEXT_ALIGN_START ? box.origin.x : box.origin.x + (box.size.width - width) / 2;
    if (align == GR_TEXT_ALIGN_END)
        x = box.origin.x + box.size.width - width;
    for (size_t i = 0; i < n; ++i)
        if (value[i] != ' ')
            draw_fill(c,
                      rounded(EX_RECT(x + (double)i * pitch + (pitch - dot) / 2,
                                      box.origin.y + (box.size.height - dot) / 2, dot, dot),
                              dot / 2, GR_CORNERS_ALL),
                      GR_FILL_NONZERO, paint(alpha(color, color.a * 0.5f)));
}

static void draw_label(void *userdata, gr_canvas *canvas, const gr_draw_info *info)
{
    const label *t = userdata;
    if (!t->value[0])
        return;
    if (!t->text)
    {
        draw_placeholder(canvas, t->value, info->bounds, t->size, t->color, t->align);
        return;
    }
    gr_text_layout *layout =
        layout_text(t->text, t->value, t->size, t->weight, t->spacing, info->bounds.size.width, t->align);
    if (!layout)
        return;
    double height = gr_text_layout_get_metrics(layout).size.height;
    EX_OK(gr_canvas_draw_text_layout(canvas, layout, EX_POINT(0, (info->bounds.size.height - height) / 2), t->color));
    gr_text_layout_unref(layout);
}

label *label_new(example *ex, gr_layer *parent, double size, uint16_t weight, gr_color color, gr_text_align align,
                 const char *value)
{
    label *t = calloc(1, sizeof(*t));
    if (!t)
        abort();
    *t = (label){.text = ex->text, .size = size, .weight = weight, .color = color, .align = align};
    snprintf(t->value, sizeof(t->value), "%s", value);
    t->layer = new_layer(ex, GR_LAYER_CONTENT);
    gr_drawer drawer = {t, draw_label, free};
    EX_OK(gr_content_layer_set_drawer(t->layer, &drawer));
    EX_OK(gr_layer_add_child(parent, t->layer));
    return t;
}

void label_set(label *t, const char *format, ...)
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

void label_color(label *t, gr_color color)
{
    if (memcmp(&color, &t->color, sizeof(color)) == 0)
        return;
    t->color = color;
    EX_OK(gr_content_layer_set_needs_display(t->layer));
}

void label_frame(label *t, gr_rect rect)
{
    EX_OK(gr_layer_set_frame(t->layer, rect));
}

double text_width(gr_text_system *text, const char *value, double size, uint16_t weight)
{
    if (!value[0])
        return 0;
    if (!text)
        return (double)strlen(value) * size * 0.55;
    gr_text_layout *layout = layout_text(text, value, size, weight, 0, 0, GR_TEXT_ALIGN_START);
    if (!layout)
        return 0;
    double w = gr_text_layout_get_metrics(layout).size.width;
    gr_text_layout_unref(layout);
    return w;
}

void draw_centered_text(gr_canvas *c, gr_text_system *text, const char *value, gr_rect box, double size,
                        uint16_t weight, gr_color color)
{
    if (!text)
    {
        draw_placeholder(c, value, box, size, color, GR_TEXT_ALIGN_CENTER);
        return;
    }
    gr_text_layout *layout = layout_text(text, value, size, weight, 0, box.size.width, GR_TEXT_ALIGN_CENTER);
    if (!layout)
        return;
    gr_text_layout_metrics metrics = gr_text_layout_get_metrics(layout);
    /* Center the digits' cap height rather than the line box, which carries descender space. */
    double cap = size * 0.70, baseline = box.origin.y + (box.size.height + cap) / 2;
    EX_OK(gr_canvas_draw_text_layout(c, layout, EX_POINT(box.origin.x, baseline - metrics.first_baseline), color));
    gr_text_layout_unref(layout);
}

/* Icons, drawn in a square box. */

static gr_stroke *round_stroke(gr_color color, double width)
{
    return stroke(paint(color), width, GR_CAP_ROUND, GR_JOIN_ROUND, (gr_double_list){0}, 0);
}

static void draw_flag(gr_canvas *c, double x, double y, double s, gr_color pole, gr_color cloth)
{
    gr_path_builder *b = gr_path_builder_new();
    move_to(b, EX_POINT(x + 0.34 * s, y + 0.14 * s));
    curve_to(b, EX_POINT(x + 0.52 * s, y + 0.08 * s), EX_POINT(x + 0.62 * s, y + 0.24 * s),
             EX_POINT(x + 0.84 * s, y + 0.22 * s));
    line_to(b, EX_POINT(x + 0.76 * s, y + 0.38 * s));
    line_to(b, EX_POINT(x + 0.84 * s, y + 0.54 * s));
    curve_to(b, EX_POINT(x + 0.62 * s, y + 0.56 * s), EX_POINT(x + 0.52 * s, y + 0.40 * s),
             EX_POINT(x + 0.34 * s, y + 0.46 * s));
    close_path(b);
    draw_fill(c, finish_path(b), GR_FILL_NONZERO, paint(cloth));
    gr_path_builder *p = gr_path_builder_new();
    move_to(p, EX_POINT(x + 0.34 * s, y + 0.12 * s));
    line_to(p, EX_POINT(x + 0.34 * s, y + 0.86 * s));
    draw_stroke(c, finish_path(p), round_stroke(pole, s * 0.075));
}

static void draw_mine(gr_canvas *c, double x, double y, double s, gr_color color)
{
    gr_point o = {x + s / 2, y + s / 2};
    gr_path_builder *b = gr_path_builder_new();
    for (int i = 0; i < 4; ++i)
    {
        double a = (double)i * EX_PI / 4, r = s * 0.36;
        move_to(b, EX_POINT(o.x - cos(a) * r, o.y - sin(a) * r));
        line_to(b, EX_POINT(o.x + cos(a) * r, o.y + sin(a) * r));
    }
    draw_stroke(c, finish_path(b), round_stroke(color, s * 0.075));
    draw_fill(c, circle(o, s * 0.25), GR_FILL_NONZERO, paint(color));
    draw_fill(c, circle(EX_POINT(o.x - s * 0.08, o.y - s * 0.08), s * 0.065), GR_FILL_NONZERO,
              paint(alpha(EX_WHITE, 0.75f)));
}

static void draw_clock(gr_canvas *c, double x, double y, double s, gr_color color)
{
    gr_point o = {x + s / 2, y + s / 2};
    draw_stroke(c, circle(o, s * 0.38), round_stroke(color, s * 0.09));
    gr_path_builder *b = gr_path_builder_new();
    move_to(b, EX_POINT(o.x, o.y - s * 0.22));
    line_to(b, o);
    line_to(b, EX_POINT(o.x + s * 0.15, o.y + s * 0.08));
    draw_stroke(c, finish_path(b), round_stroke(color, s * 0.09));
}

void draw_icon(gr_canvas *c, icon_kind kind, gr_rect box, gr_color color)
{
    double s = fmin(box.size.width, box.size.height);
    double x = box.origin.x + (box.size.width - s) / 2, y = box.origin.y + (box.size.height - s) / 2;
    switch (kind)
    {
        case ICON_FLAG:
            draw_flag(c, x, y, s, hex(C_SLATE), color);
            break;
        case ICON_MINE:
            draw_mine(c, x, y, s, color);
            break;
        case ICON_CLOCK:
            draw_clock(c, x, y, s, color);
            break;
        case ICON_WRONG:
        {
            gr_canvas_save(c);
            EX_OK(gr_canvas_set_alpha(c, 0.35f));
            draw_flag(c, x, y, s, hex(C_SLATE), hex(C_FLAG));
            gr_canvas_restore(c);
            gr_path_builder *b = gr_path_builder_new();
            move_to(b, EX_POINT(x + 0.24 * s, y + 0.24 * s));
            line_to(b, EX_POINT(x + 0.76 * s, y + 0.76 * s));
            move_to(b, EX_POINT(x + 0.76 * s, y + 0.24 * s));
            line_to(b, EX_POINT(x + 0.24 * s, y + 0.76 * s));
            draw_stroke(c, finish_path(b), round_stroke(color, s * 0.09));
            break;
        }
    }
}

typedef struct
{
    icon_kind kind;
    gr_color color;
} icon;

static void draw_icon_layer(void *userdata, gr_canvas *canvas, const gr_draw_info *info)
{
    const icon *i = userdata;
    draw_icon(canvas, i->kind, info->bounds, i->color);
}

gr_layer *icon_new(example *ex, gr_layer *parent, icon_kind kind, gr_color color)
{
    icon *i = malloc(sizeof(*i));
    if (!i)
        abort();
    *i = (icon){kind, color};
    gr_layer *l = new_layer(ex, GR_LAYER_CONTENT);
    gr_drawer drawer = {i, draw_icon_layer, free};
    EX_OK(gr_content_layer_set_drawer(l, &drawer));
    EX_OK(gr_layer_add_child(parent, l));
    return l;
}

/* Buttons */

static const spring_params HOVER = {0.24, 1}, PRESS = {0.2, 0.62};

void button_init(example *ex, button *b, gr_layer *parent, const char *title, const char *detail, bool primary)
{
    b->primary = primary;
    b->layer = pill(ex, parent, EX_RECT(0, 0, 10, 10), primary ? hex(C_ACCENT) : hex(C_PANEL), 12);
    if (primary)
        drop_shadow(b->layer, alpha(hex(C_ACCENT_DEEP), 0.28f), 0, 4, 8, NULL);
    else
    {
        border(b->layer, 1, hex(C_LINE));
        drop_shadow(b->layer, alpha(hex(0x6B6455), 0.08f), 0, 2, 4, NULL);
    }
    b->title = label_new(ex, b->layer, 15, 600, primary ? EX_WHITE : hex(C_INK), GR_TEXT_ALIGN_CENTER, title);
    if (detail)
        b->detail = label_new(ex, b->layer, 12, 500, hex(C_MUTED), GR_TEXT_ALIGN_CENTER, detail);
}

void button_layout(button *b, gr_rect r)
{
    b->rect = r;
    EX_OK(gr_layer_set_frame(b->layer, r));
    EX_OK(gr_layer_set_corner_radius(b->layer, fmin(14, r.size.height / 2)));
    double w = r.size.width, h = r.size.height;
    if (b->detail)
    {
        label_frame(b->title, EX_RECT(6, h * 0.16, w - 12, h * 0.40));
        label_frame(b->detail, EX_RECT(6, h * 0.52, w - 12, h * 0.30));
    }
    else
        label_frame(b->title, EX_RECT(6, 0, w - 12, h));
}

void button_update(button *b, int steps)
{
    spring_run(&b->hover, HOVER, steps);
    spring_run(&b->press, PRESS, steps);
    double s = 1 + 0.02 * b->hover.value - 0.05 * b->press.value;
    gr_affine t = scaling(s);
    t.ty = -1.5 * b->hover.value * (1 - b->press.value);
    EX_OK(gr_layer_set_transform(b->layer, t));
    gr_color c =
        b->primary ? mix(hex(C_ACCENT), hex(0x7DAA9A), b->hover.value) : mix(hex(C_PANEL), EX_WHITE, b->hover.value);
    background(b->layer, mix(c, b->primary ? hex(C_ACCENT_DEEP) : hex(0xE9E6DF), b->press.value));
}

bool button_hit(const button *b, gr_point p)
{
    return inside(b->rect, p);
}

void button_hover(button *b, bool hovered)
{
    spring_to(&b->hover, hovered ? 1 : 0);
}
