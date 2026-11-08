/* Size picker: numeric fields for columns, rows and mines with a gliding focus ring, a live preview of the field and
 * classic presets. */
#include "mines.h"

static const struct
{
    const char *title, *detail;
    int cols, rows;
} presets[PRESETS] = {
    {"Easy", "9 × 9 · 10 mines", 9, 9},
    {"Medium", "16 × 16 · 40 mines", 16, 16},
    {"Hard", "30 × 16 · 99 mines", 30, 16},
};

static const spring_params RING = {0.34, 0.78}, SHAKE = {0.12, 0.16}, ERROR = {0.6, 1}, POP = {0.28, 0.42},
                           LIT = {0.22, 1}, PREVIEW = {0.42, 0.72};

#define FIELD_W 112.0
#define FIELD_H 84.0
#define MINES 2

static field *focused_field(setup_page *s)
{
    return s->focus >= 0 && s->focus < FIELDS ? &s->fields[s->focus] : NULL;
}

/* Preview */

static uint32_t hash2(int x, int y)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void draw_preview(void *userdata, gr_canvas *canvas, const gr_draw_info *info)
{
    const setup_page *s = userdata;
    int cols = s->fields[0].value, rows = s->fields[1].value;
    double w = info->bounds.size.width, h = info->bounds.size.height;
    if (w < 1 || h < 1)
        return;
    double cw = w / cols, ch = h / rows, size = fmin(cw, ch);
    double gap = size >= 6 ? fmax(1, size * 0.12) : size >= 3 ? 0.5 : 0, radius = fmin(size * 0.22, 3);
    static const uint32_t colors[5] = {C_HIDDEN, C_OPEN, 0xE1EED8, 0xEEEACF, 0xF3E0CE};
    gr_path_builder *buckets[5];
    bool used[5] = {0};
    for (int i = 0; i < 5; ++i)
        buckets[i] = gr_path_builder_new();
    /* A cleared pocket with number tiles around its edge, like a game in progress. */
    double ox = 0.40, oy = 0.56, scale = fmax(cols, rows);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
        {
            uint32_t n = hash2(c, r);
            double u = ((c + 0.5) / cols - ox) * cols / scale, v = ((r + 0.5) / rows - oy) * rows / scale;
            double d = sqrt(u * u + v * v) + (double)(n & 255) / 255.0 * 0.07;
            int bucket = d < 0.2 ? 1 : d < 0.26 ? 2 + (int)((n >> 8) % 3) : 0;
            if (cols * rows < 4)
                bucket = 0;
            gr_rect cell = EX_RECT(c * cw + gap / 2, r * ch + gap / 2, cw - gap, ch - gap);
            if (radius >= 1)
                EX_OK(gr_path_builder_add_rounded_rect(buckets[bucket], cell, radius, GR_CORNERS_ALL));
            else
                EX_OK(gr_path_builder_add_rect(buckets[bucket], cell));
            used[bucket] = true;
        }
    for (int i = 0; i < 5; ++i)
    {
        gr_path *p = finish_path(buckets[i]);
        if (used[i] && p)
            draw_fill(canvas, p, GR_FILL_NONZERO, paint(hex(colors[i])));
        else
            gr_path_unref(p);
    }
}

static gr_size preview_size(const setup_page *s)
{
    int cols = s->fields[0].value, rows = s->fields[1].value;
    double area_w = s->preview_area.size.width * 0.66, area_h = s->preview_area.size.height;
    double cell = fmin(fmin(area_w / cols, area_h / rows), 15);
    return (gr_size){cell * cols, cell * rows};
}

/* Fields */

static void update_hint(setup_page *s)
{
    int cols = s->fields[0].value, rows = s->fields[1].value, n = cols * rows, mines = s->fields[MINES].value;
    int suggested = mine_count(cols, rows);
    if (mines == suggested)
        label_set(s->hint, "%.1f%% of %d tiles", 100.0 * mines / n, n);
    else
        label_set(s->hint, "%.1f%% of %d tiles  ·  %d suggested", 100.0 * mines / n, n, suggested);
}

static void field_text(mines *m, field *f, const char *text)
{
    snprintf(f->text, sizeof(f->text), "%s", text);
    /* Four digits only fit the box a size smaller. */
    f->digits_size = strlen(f->text) >= 4 ? 31 : 40;
    f->digits->size = f->digits_size;
    label_set(f->digits, "%s", f->text);
    EX_OK(gr_content_layer_set_needs_display(f->digits->layer));
    f->text_w = text_width(m->ex->text, f->text, f->digits_size, 600);
    m->setup.blink_epoch = m->now;
}

static void field_show(mines *m, field *f, int value)
{
    char text[8];
    snprintf(text, sizeof(text), "%d", value);
    f->value = value;
    field_text(m, f, text);
}

static void field_reject(field *f)
{
    spring_kick(&f->shake, 260);
    spring_set(&f->error, 1, 0);
}

static void field_pop(field *f)
{
    spring_set(&f->pop, 0.86, 1);
}

/* The size changed: the mine count follows the suggestion unless it was typed in, and always stays in range. */
static void size_changed(mines *m)
{
    setup_page *s = &m->setup;
    int cols = s->fields[0].value, rows = s->fields[1].value;
    field *f = &s->fields[MINES];
    f->max = max_mines(cols, rows);
    f->min = f->max > 0 ? 1 : 0;
    int value = s->custom_mines ? (int)clampd(f->value, f->min, f->max) : mine_count(cols, rows);
    if (value != f->value || atoi(f->text) != value)
    {
        field_show(m, f, value);
        if (s->focus == MINES)
            f->fresh = true;
        field_pop(f);
    }
    gr_size size = preview_size(s);
    spring_to(&s->pw, size.width);
    spring_to(&s->ph, size.height);
    EX_OK(gr_content_layer_set_needs_display(s->preview));
}

/* A field took a new valid value. */
static void field_changed(mines *m, int i)
{
    setup_page *s = &m->setup;
    if (i == MINES)
        s->custom_mines = s->fields[MINES].value != mine_count(s->fields[0].value, s->fields[1].value);
    else
        size_changed(m);
    update_hint(s);
}

static void field_set(mines *m, int i, int value)
{
    field_show(m, &m->setup.fields[i], value);
    field_changed(m, i);
}

static void field_digit(mines *m, int i, int digit)
{
    field *f = &m->setup.fields[i];
    bool replace = f->fresh || !f->text[0];
    long value = replace ? digit : atol(f->text) * 10 + digit;
    /* No leading zeros, nothing past the maximum. */
    if ((replace && digit == 0 && f->min > 0) || value > f->max || (!replace && atoi(f->text) == 0))
    {
        field_reject(f);
        return;
    }
    f->fresh = false;
    field_show(m, f, (int)value);
    field_pop(f);
    if (f->value >= f->min)
        field_changed(m, i);
}

static void field_backspace(mines *m, int i)
{
    field *f = &m->setup.fields[i];
    char text[8];
    if (f->fresh)
        text[0] = 0;
    else
    {
        snprintf(text, sizeof(text), "%s", f->text);
        size_t n = strlen(text);
        if (!n)
        {
            field_reject(f);
            return;
        }
        text[n - 1] = 0;
    }
    f->fresh = false;
    field_text(m, f, text);
    if (text[0] && atoi(text) >= f->min)
    {
        f->value = atoi(text);
        field_changed(m, i);
    }
}

static void field_step(mines *m, int i, int delta)
{
    field *f = &m->setup.fields[i];
    int value = (int)clampd(f->value + delta, f->min, f->max);
    if (value == f->value)
    {
        field_reject(f);
        return;
    }
    field_set(m, i, value);
    f->fresh = true;
    field_pop(f);
}

/* A field that is empty or below its minimum falls back to its last valid value when it loses focus. */
static void field_commit(mines *m, int i)
{
    field *f = &m->setup.fields[i];
    if (!f->text[0] || atoi(f->text) != f->value)
        field_set(m, i, f->value);
}

/* Focus */

static const button *focus_button(const setup_page *s, int i)
{
    return i == START ? &s->start : &s->presets[i - START - 1];
}

static gr_rect focus_rect(const setup_page *s, int i, double *radius)
{
    if (i < FIELDS)
    {
        *radius = 18 + 6;
        return inset(s->fields[i].rect, -6);
    }
    const button *b = focus_button(s, i);
    *radius = fmin(14, b->rect.size.height / 2) + 6;
    return inset(b->rect, -6);
}

static void ring_target(setup_page *s, bool snap)
{
    double radius;
    gr_rect r = focus_rect(s, s->focus, &radius);
    spring *springs[5] = {&s->rx, &s->ry, &s->rw, &s->rh, &s->rr};
    double targets[5] = {r.origin.x, r.origin.y, r.size.width, r.size.height, radius};
    for (int i = 0; i < 5; ++i)
        if (snap)
            spring_set(springs[i], targets[i], targets[i]);
        else
            spring_to(springs[i], targets[i]);
}

/* Moves focus to i, or clears it with -1; the ring glides between controls and fades in or out. */
static void set_focus(mines *m, int i)
{
    setup_page *s = &m->setup;
    if (i >= 0)
        i %= FOCUSABLES;
    if (i == s->focus)
        return;
    bool appearing = s->focus < 0;
    field *old = focused_field(s);
    if (old)
    {
        field_commit(m, s->focus);
        old->fresh = false;
        spring_to(&old->lit, 0);
    }
    s->focus = i;
    field *f = focused_field(s);
    if (f)
    {
        f->fresh = f->text[0] != 0;
        spring_to(&f->lit, 1);
    }
    s->blink_epoch = m->now;
    if (i < 0)
        return;
    ring_target(s, appearing);
    if (appearing)
    {
        /* Settle in from slightly outside the control. */
        s->rx.value -= 8;
        s->ry.value -= 8;
        s->rw.value += 16;
        s->rh.value += 16;
        spring *springs[4] = {&s->rx, &s->ry, &s->rw, &s->rh};
        for (int k = 0; k < 4; ++k)
            springs[k]->active = true;
    }
}

static int focus_step(const setup_page *s, int delta)
{
    if (s->focus < 0)
        return delta > 0 ? 0 : FOCUSABLES - 1;
    return ((s->focus + delta) % FOCUSABLES + FOCUSABLES) % FOCUSABLES;
}

static void start(mines *m)
{
    setup_page *s = &m->setup;
    for (int i = 0; i < FIELDS; ++i)
        field_commit(m, i);
    go_game(m, s->fields[0].value, s->fields[1].value, s->fields[MINES].value);
}

static void activate(mines *m, int i)
{
    if (i > START && i < FOCUSABLES)
    {
        int p = i - START - 1;
        field_set(m, 0, presets[p].cols);
        field_set(m, 1, presets[p].rows);
        field_set(m, MINES, mine_count(presets[p].cols, presets[p].rows));
    }
    start(m);
}

/* Build and layout */

void setup_build(mines *m)
{
    example *ex = m->ex;
    setup_page *s = &m->setup;
    gr_layer *root = gr_scene_get_root(ex->scene);
    s->page = layer(ex, root, EX_RECT(0, 0, m->view.width, m->view.height));
    s->content = layer(ex, s->page, EX_RECT(0, 0, m->view.width, m->view.height));
    EX_OK(gr_layer_set_anchor_point(s->content, EX_POINT(0, 0)));
    s->window_focused = true;
    s->scale = 1;

    s->title = label_new(ex, s->content, 34, 700, hex(C_INK), GR_TEXT_ALIGN_CENTER, "Mines");
    s->title->spacing = -0.5;
    s->subtitle = label_new(ex, s->content, 14, 400, hex(C_MUTED), GR_TEXT_ALIGN_CENTER,
                            "Choose a field and how many mines hide in it.");

    s->preview = new_layer(ex, GR_LAYER_CONTENT);
    EX_OK(gr_layer_add_child(s->content, s->preview));
    gr_drawer drawer = {s, draw_preview, NULL};
    EX_OK(gr_content_layer_set_drawer(s->preview, &drawer));

    static const char *const captions[FIELDS] = {"COLUMNS", "ROWS", "MINES"};
    static const int initial[FIELDS] = {16, 16, 40};
    for (int i = 0; i < FIELDS; ++i)
    {
        field *f = &s->fields[i];
        f->box = pill(ex, s->content, EX_RECT(0, 0, FIELD_W, FIELD_H), hex(C_PANEL), 18);
        border(f->box, 1.5, hex(C_LINE));
        drop_shadow(f->box, alpha(hex(0x6B6455), 0.07f), 0, 3, 6, NULL);
        f->selection = pill(ex, f->box, EX_RECT(0, 0, 10, 50), alpha(hex(C_ACCENT), 0.2f), 10);
        f->digits = label_new(ex, f->box, 40, 600, hex(C_INK), GR_TEXT_ALIGN_CENTER, "");
        f->caret = pill(ex, f->box, EX_RECT(0, 0, 2.5, 40), hex(C_ACCENT), 1.25);
        f->caption = label_new(ex, s->content, 11, 600, hex(C_MUTED), GR_TEXT_ALIGN_CENTER, captions[i]);
        f->caption->spacing = 1.4;
        f->value = initial[i];
        f->min = 1;
        f->max = i == MINES ? max_mines(16, 16) : MAX_SIDE;
        f->pop.value = f->pop.target = 1;
    }
    s->rule_mid = layer(ex, s->content, EX_RECT(0, 0, 1, 1));
    background(s->rule_mid, hex(C_LINE));
    s->times = label_new(ex, s->content, 28, 300, hex(C_MUTED), GR_TEXT_ALIGN_CENTER, "×");
    s->hint = label_new(ex, s->content, 13, 500, hex(C_MUTED), GR_TEXT_ALIGN_CENTER, "");

    button_init(ex, &s->start, s->content, "Start sweeping", NULL, true);
    s->divider = label_new(ex, s->content, 12, 500, hex(C_MUTED), GR_TEXT_ALIGN_CENTER, "or pick a classic");
    s->rule_left = layer(ex, s->content, EX_RECT(0, 0, 1, 1));
    s->rule_right = layer(ex, s->content, EX_RECT(0, 0, 1, 1));
    background(s->rule_left, hex(C_LINE));
    background(s->rule_right, hex(C_LINE));
    for (int i = 0; i < PRESETS; ++i)
        button_init(ex, &s->presets[i], s->content, presets[i].title, presets[i].detail, false);

    s->ring = layer(ex, s->content, EX_RECT(0, 0, 10, 10));
    border(s->ring, 2.5, hex(C_ACCENT));
    s->ring_alpha.value = s->ring_alpha.target = 1;

    setup_layout(m);
    for (int i = 0; i < FIELDS; ++i)
        field_set(m, i, s->fields[i].value);
    s->fields[0].fresh = true;
    s->fields[0].lit.value = s->fields[0].lit.target = 1;
    gr_size size = preview_size(s);
    spring_set(&s->pw, size.width, size.width);
    spring_set(&s->ph, size.height, size.height);
}

void setup_layout(mines *m)
{
    setup_page *s = &m->setup;
    const double fixed = 438, min_w = 480, min_h = fixed + 32;
    EX_OK(gr_layer_set_frame(s->page, EX_RECT(0, 0, m->view.width, m->view.height)));
    /* Below the column's natural size, lay it out larger and scale it down to fit. */
    s->scale = fmin(1, fmin(m->view.width / min_w, m->view.height / min_h));
    double w = m->view.width / s->scale, h = m->view.height / s->scale, cx = w / 2, cw = fmin(w - 48, 440);
    EX_OK(gr_layer_set_position(s->content, EX_POINT(0, 0)));
    EX_OK(gr_layer_set_bounds(s->content, EX_RECT(0, 0, w, h)));
    EX_OK(gr_layer_set_transform(s->content, scaling(s->scale)));
    double preview_h = clampd(h - fixed - 24 - 96, 0, 150);
    if (preview_h < 48)
        preview_h = 0;
    double y = fmax(16, (h - fixed - (preview_h ? 24 + preview_h : 0)) / 2);

    label_frame(s->title, EX_RECT(cx - cw / 2, y, cw, 44));
    y += 48;
    label_frame(s->subtitle, EX_RECT(cx - cw / 2 - 40, y, cw + 80, 20));
    y += 20;
    if (preview_h)
        y += 24;
    s->preview_area = EX_RECT(cx - cw / 2, y, cw, preview_h);
    EX_OK(gr_layer_set_hidden(s->preview, preview_h == 0));
    y += preview_h + 28;

    /* columns × rows | mines */
    const double times_w = 44, split_w = 37;
    double fx = cx - (3 * FIELD_W + times_w + split_w) / 2;
    const double xs[FIELDS] = {fx, fx + FIELD_W + times_w, fx + 2 * FIELD_W + times_w + split_w};
    for (int i = 0; i < FIELDS; ++i)
    {
        field *f = &s->fields[i];
        f->rect = EX_RECT(xs[i], y, FIELD_W, FIELD_H);
        EX_OK(gr_layer_set_frame(f->box, f->rect));
        label_frame(f->digits, EX_RECT(0, 0, FIELD_W, FIELD_H));
        label_frame(f->caption, EX_RECT(f->rect.origin.x, y + FIELD_H + 10, FIELD_W, 16));
    }
    label_frame(s->times, EX_RECT(fx + FIELD_W, y, times_w, FIELD_H));
    EX_OK(gr_layer_set_frame(s->rule_mid, EX_RECT(xs[2] - split_w / 2 - 0.5, y + 20, 1, FIELD_H - 40)));
    y += FIELD_H + 8 + 16 + 14;
    label_frame(s->hint, EX_RECT(cx - cw / 2, y, cw, 20));
    y += 40;
    button_layout(&s->start, EX_RECT(cx - 110, y, 220, 48));
    y += 48 + 28;
    label_frame(s->divider, EX_RECT(cx - 80, y, 160, 16));
    EX_OK(gr_layer_set_frame(s->rule_left, EX_RECT(cx - cw / 2, y + 8, cw / 2 - 86, 1)));
    EX_OK(gr_layer_set_frame(s->rule_right, EX_RECT(cx + 86, y + 8, cw / 2 - 86, 1)));
    y += 32;
    double pw = fmin((cw - 24) / 3, 140), x0 = cx - (3 * pw + 24) / 2;
    for (int i = 0; i < PRESETS; ++i)
        button_layout(&s->presets[i], EX_RECT(x0 + i * (pw + 12), y, pw, 72));

    if (s->focus >= 0)
        ring_target(s, true);
    gr_size size = preview_size(s);
    spring_set(&s->pw, size.width, size.width);
    spring_set(&s->ph, size.height, size.height);
}

void setup_update(mines *m, int steps)
{
    setup_page *s = &m->setup;
    for (int i = 0; i < FIELDS; ++i)
    {
        field *f = &s->fields[i];
        spring_run(&f->shake, SHAKE, steps);
        spring_run(&f->error, ERROR, steps);
        spring_run(&f->pop, POP, steps);
        spring_run(&f->lit, LIT, steps);
        EX_OK(gr_layer_set_transform(f->box, translation(EX_POINT(f->shake.value, 0))));
        background(f->box, mix(hex(C_PANEL), EX_WHITE, f->lit.value));
        border(f->box, 1.5,
               mix(mix(hex(C_LINE), alpha(hex(C_ACCENT), 0.5f), f->lit.value), hex(C_BOOM), f->error.value));
        label_color(f->digits, mix(hex(C_INK), hex(C_BOOM), f->error.value * 0.8));
        EX_OK(gr_layer_set_transform(f->digits->layer, scaling(f->pop.value)));

        bool focused = s->focus == i;
        bool selected = focused && f->fresh && f->text[0];
        EX_OK(gr_layer_set_hidden(f->selection, !selected));
        if (selected)
        {
            double sw = f->text_w + 18;
            EX_OK(gr_layer_set_frame(f->selection, EX_RECT((FIELD_W - sw) / 2, (FIELD_H - 50) / 2, sw, 50)));
            EX_OK(gr_layer_set_transform(f->selection, scaling(f->pop.value)));
        }
        bool caret = focused && !f->fresh;
        EX_OK(gr_layer_set_hidden(f->caret, !caret));
        if (caret)
        {
            double x = FIELD_W / 2 + f->text_w / 2 + (f->text[0] ? 3 : -1);
            double h = f->digits_size;
            EX_OK(gr_layer_set_frame(f->caret, EX_RECT(x, (FIELD_H - h) / 2, 2.5, h)));
            double phase = fmod(m->now - s->blink_epoch, 1.1) / 1.1;
            float blink = (float)clampd(1.6 * cos(EX_TAU * phase) + 0.6, 0, 1);
            EX_OK(gr_layer_set_opacity(f->caret, s->window_focused ? blink : 0));
        }
    }

    spring_run(&s->rx, RING, steps);
    spring_run(&s->ry, RING, steps);
    spring_run(&s->rw, RING, steps);
    spring_run(&s->rh, RING, steps);
    spring_run(&s->rr, RING, steps);
    spring_to(&s->ring_alpha, s->focus < 0 ? 0 : s->window_focused ? 1 : 0.3);
    spring_run(&s->ring_alpha, LIT, steps);
    EX_OK(gr_layer_set_frame(s->ring, EX_RECT(s->rx.value, s->ry.value, fmax(0, s->rw.value), fmax(0, s->rh.value))));
    EX_OK(gr_layer_set_corner_radius(s->ring, fmax(0, s->rr.value)));
    EX_OK(gr_layer_set_opacity(s->ring, (float)s->ring_alpha.value));
    /* The ring follows a pressed button down. */
    gr_affine ring = scaling(1);
    if (s->focus >= START)
        ring = scaling(1 - 0.04 * focus_button(s, s->focus)->press.value);
    else if (s->focus >= 0)
        ring = translation(EX_POINT(s->fields[s->focus].shake.value, 0));
    EX_OK(gr_layer_set_transform(s->ring, ring));

    spring_run(&s->pw, PREVIEW, steps);
    spring_run(&s->ph, PREVIEW, steps);
    gr_rect a = s->preview_area;
    double pw = fmax(1, s->pw.value), ph = fmax(1, s->ph.value);
    EX_OK(gr_layer_set_frame(
        s->preview, EX_RECT(a.origin.x + (a.size.width - pw) / 2, a.origin.y + (a.size.height - ph) / 2, pw, ph)));

    button_update(&s->start, steps);
    for (int i = 0; i < PRESETS; ++i)
        button_update(&s->presets[i], steps);
}

/* Input */

static button *button_index(setup_page *s, int i)
{
    return i == START ? &s->start : &s->presets[i - START - 1];
}

static int button_at(setup_page *s, gr_point p)
{
    for (int i = START; i < FOCUSABLES; ++i)
        if (button_hit(button_index(s, i), p))
            return i;
    return -1;
}

void setup_reset_pointer(mines *m)
{
    setup_page *s = &m->setup;
    for (int i = START; i < FOCUSABLES; ++i)
    {
        button *b = button_index(s, i);
        b->held = false;
        button_hover(b, false);
        spring_to(&b->press, 0);
    }
}

static bool pointer(mines *m, const gr_event *e)
{
    setup_page *s = &m->setup;
    const gr_pointer_event *p = &e->value.pointer;
    gr_point at = {p->position.x / s->scale, p->position.y / s->scale};
    bool gone = e->type == GR_EVENT_POINTER_LEAVE || e->type == GR_EVENT_POINTER_CANCEL;
    int hit = gone ? -1 : button_at(s, at);
    int field_hit = -1;
    for (int i = 0; i < FIELDS && !gone; ++i)
        if (inside(s->fields[i].rect, at))
            field_hit = i;
    for (int i = START; i < FOCUSABLES; ++i)
        button_hover(button_index(s, i), i == hit);
    set_cursor(m, field_hit >= 0 ? GR_CURSOR_TEXT : hit >= 0 ? GR_CURSOR_POINTER : GR_CURSOR_DEFAULT);

    if (e->type == GR_EVENT_POINTER_DOWN && p->button == GR_BUTTON_PRIMARY)
    {
        if (field_hit >= 0)
        {
            /* Clicking into a field selects its text; clicking again puts the caret at the end, a double click
             * selects again. */
            bool was = s->focus == field_hit;
            set_focus(m, field_hit);
            field *f = &s->fields[field_hit];
            if (was)
                f->fresh = p->click_count >= 2 && f->text[0];
            s->blink_epoch = m->now;
        }
        if (hit >= 0)
        {
            /* A click on a button only takes focus away from the fields; the ring is for keyboard focus. */
            button *b = button_index(s, hit);
            b->held = true;
            spring_to(&b->press, 1);
            set_focus(m, -1);
        }
    }
    if (e->type == GR_EVENT_POINTER_UP || gone)
        for (int i = START; i < FOCUSABLES; ++i)
        {
            button *b = button_index(s, i);
            if (!b->held)
                continue;
            b->held = false;
            spring_to(&b->press, 0);
            if (e->type == GR_EVENT_POINTER_UP && i == hit)
                activate(m, i);
        }
    return true;
}

static bool scroll(mines *m, const gr_event *e)
{
    static double accumulated;
    setup_page *s = &m->setup;
    const gr_scroll_event *sc = &e->value.scroll;
    gr_point at = {sc->position.x / s->scale, sc->position.y / s->scale};
    for (int i = 0; i < FIELDS; ++i)
    {
        if (!inside(s->fields[i].rect, at))
            continue;
        if (s->focus != i)
            set_focus(m, i);
        accumulated += sc->unit == GR_SCROLL_UNIT_LINE ? sc->delta.y : sc->delta.y / 24;
        while (fabs(accumulated) >= 1)
        {
            int step = accumulated > 0 ? -1 : 1;
            accumulated += step;
            field_step(m, i, step);
        }
        return true;
    }
    return false;
}

static bool key(mines *m, const gr_event *e)
{
    setup_page *s = &m->setup;
    const gr_key_event *k = &e->value.key;
    if (e->modifiers & (GR_MODIFIER_ALT | GR_MODIFIER_SUPER | GR_MODIFIER_CONTROL))
        return false;
    if (e->type != GR_EVENT_KEY_DOWN)
        return true;
    field *f = focused_field(s);
    switch (k->key)
    {
        case GR_KEY_TAB:
            set_focus(m, focus_step(s, e->modifiers & GR_MODIFIER_SHIFT ? -1 : 1));
            return true;
        case GR_KEY_ENTER:
            activate(m, s->focus < 0 ? START : s->focus);
            return true;
        case GR_KEY_ARROW_UP:
        case GR_KEY_ARROW_DOWN:
        {
            bool up = k->key == GR_KEY_ARROW_UP;
            if (f)
                field_step(m, s->focus, (up ? 1 : -1) * (e->modifiers & GR_MODIFIER_SHIFT ? 10 : 1));
            else if (s->focus < 0)
                set_focus(m, START);
            else
                set_focus(m, up ? (s->focus > START ? START : 0) : (s->focus == START ? START + 1 : START));
            return true;
        }
        case GR_KEY_ARROW_LEFT:
        case GR_KEY_ARROW_RIGHT:
        {
            int d = k->key == GR_KEY_ARROW_RIGHT ? 1 : -1;
            if (s->focus >= 0 && s->focus < FIELDS)
                set_focus(m, (int)clampd(s->focus + d, 0, FIELDS - 1));
            else if (s->focus > START)
                set_focus(m, (int)clampd(s->focus + d, START + 1, FOCUSABLES - 1));
            return true;
        }
        case GR_KEY_BACKSPACE:
        case GR_KEY_DELETE:
            if (f)
                field_backspace(m, s->focus);
            return true;
        case GR_KEY_CHARACTER:
            if (k->character >= '0' && k->character <= '9')
            {
                if (f)
                    field_digit(m, s->focus, (int)(k->character - '0'));
                return true;
            }
            if (k->character == ' ' && s->focus >= START)
            {
                activate(m, s->focus);
                return true;
            }
            return true;
        default:
            return true;
    }
}

bool setup_input(mines *m, const gr_event *e)
{
    switch (e->type)
    {
        case GR_EVENT_POINTER_DOWN:
        case GR_EVENT_POINTER_UP:
        case GR_EVENT_POINTER_MOVE:
        case GR_EVENT_POINTER_ENTER:
        case GR_EVENT_POINTER_LEAVE:
        case GR_EVENT_POINTER_CANCEL:
            return pointer(m, e);
        case GR_EVENT_SCROLL:
            return scroll(m, e);
        case GR_EVENT_KEY_DOWN:
        case GR_EVENT_KEY_UP:
            return key(m, e);
        default:
            return false;
    }
}
