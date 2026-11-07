/* Per-glyph text transitions: each label draws every glyph in its own content layer. When the text changes, glyphs
 * that stay (same font, glyph and size) glide to their new place, and the others scale, slide, fade and blur in or
 * out with a short stagger. All motion is driven by springs integrated on the Scene thread. */
#include "example.h"

#define CARD_WIDTH 440.0
#define CARD_HEIGHT 260.0
#define INSET 20.0
#define LABEL_WIDTH (CARD_WIDTH - 2 * INSET)
/* Fixed simulation step; the update callback replays elapsed time in these steps, applying text changes at their
 * scheduled times, so the result does not depend on the display rate (or on a single jump to the snapshot time). */
#define STEP (1.0 / 240)

typedef struct
{
    double response, damping; /**< Period of the undamped oscillation in seconds, and damping ratio. */
} spring_params;

static const spring_params MOVE = {0.5, 1}, SCALE = {0.3, 1}, OFFSET = {0.4, 0.54}, FADE = {0.42, 1},
                           BLUR = {0.42, 0.8};

typedef struct
{
    double value, velocity, target;
    bool active;
} spring;

static void spring_to(spring *s, double target)
{
    s->target = target;
    s->active = s->value != target || s->velocity != 0;
}

static void spring_set(spring *s, double value, double target)
{
    s->value = value;
    s->velocity = 0;
    spring_to(s, target);
}

static void spring_step(spring *s, spring_params p, double dt)
{
    if (!s->active)
        return;
    double stiffness = pow(EX_TAU / p.response, 2), friction = 2 * EX_TAU * p.damping / p.response;
    s->velocity += (-stiffness * (s->value - s->target) - friction * s->velocity) * dt;
    s->value += s->velocity * dt;
    if (fabs(s->value - s->target) < 0.01 && fabs(s->velocity) < 0.01)
    {
        s->value = s->target;
        s->velocity = 0;
        s->active = false;
    }
}

/* Drawer userdata: one glyph of a layout, with the layout box placed so the glyph lands inside the layer. */
typedef struct
{
    gr_text_layout *layout;
    size_t line, index;
    gr_point origin;
    gr_color color;
} glyph_drawing;

static void draw_glyph(void *userdata, gr_canvas *canvas, const gr_draw_info *info)
{
    (void)info;
    const glyph_drawing *g = userdata;
    EX_OK(gr_canvas_draw_text_glyphs(canvas, g->layout, g->line, (gr_glyph_range){g->index, g->index + 1}, g->origin,
                                     g->color));
}

static void free_glyph(void *userdata)
{
    glyph_drawing *g = userdata;
    gr_text_layout_unref(g->layout);
    free(g);
}

typedef struct
{
    gr_layer *layer;
    /* Identity: glyphs with equal font, glyph and size draw the same shape, so a layer can keep showing it. */
    uint64_t font;
    uint32_t glyph;
    double font_size;
    size_t order; /**< Index among the visible glyphs of the latest text. */
    gr_size size;
    spring x, y;                         /**< Layer origin in label coordinates. */
    spring scale, offset, opacity, blur; /**< offset is in thirds of the layer height. */
    double delay;                        /**< Seconds before the springs start. */
    bool leaving, claimed;
} glyph_state;

typedef struct label label;

/** Writes the text for step into buf; sets *down when the change should roll downward. */
typedef void (*label_text_fn)(size_t step, char *buf, size_t size, bool *down);

struct label
{
    gr_scene *scene;
    gr_text_system *system;
    gr_layer *layer;
    gr_text_style style;
    gr_text_layout_options options;
    gr_color color;
    label_text_fn text;
    double period, next_change;
    size_t step;
    glyph_state *glyphs;
    size_t count, capacity;
};

#define LABELS 4

typedef struct
{
    example *ex;
    label labels[LABELS];
    double time;
} glyphs_scene;

/* Text sources */

static void clock_text(size_t step, char *buf, size_t size, bool *down)
{
    size_t seconds = 57 + step;
    snprintf(buf, size, "%02zu:%02zu", seconds / 60 % 60, seconds % 60);
    *down = false;
}

static const double prices[] = {1284.50, 1302.75, 987.20, 12480.00, 12406.35, 1284.50};

static void counter_text(size_t step, char *buf, size_t size, bool *down)
{
    size_t count = sizeof(prices) / sizeof(*prices);
    double value = prices[step % count];
    long cents = lround(value * 100);
    if (cents >= 100000)
        snprintf(buf, size, "$%ld,%03ld.%02ld", cents / 100000, cents / 100 % 1000, cents % 100);
    else
        snprintf(buf, size, "$%ld.%02ld", cents / 100, cents % 100);
    *down = step > 0 && value < prices[(step - 1) % count];
}

static const char *const phrases[] = {
    "Glyph by glyph",
    "Layer blur",
    "Spring motion",
    "\xE9\x80\x90\xE5\xAD\x97\xE5\x8A\xA8\xE7\x94\xBB", /* 逐字动画 */
    "Hello \xF0\x9F\x91\x8B world",
};

static void phrase_text(size_t step, char *buf, size_t size, bool *down)
{
    snprintf(buf, size, "%s", phrases[step % (sizeof(phrases) / sizeof(*phrases))]);
    *down = false;
}

/* Successive edits, so most of each paragraph stays and reflows around the inserted and removed words. */
static const char *const paragraphs[] = {
    "Graphite draws each glyph in its own layer, so text can move, scale and blur one character at a time.",
    "Graphite draws every single glyph in its own layer, so the text can move, fade, scale and blur one character "
    "at a time.",
    "Every single glyph sits in its own layer, so the text can glide to a new line, one character at a time.",
    "Every glyph sits in its own layer \xE2\x9C\xA8 \xE6\xAF\x8F\xE4\xB8\xAA\xE5\xAD\x97\xE5\xBD\xA2\xE9\x83\xBD"
    "\xE6\x98\xAF\xE7\x8B\xAC\xE7\xAB\x8B\xE7\x9A\x84\xE5\x9B\xBE\xE5\xB1\x82\xEF\xBC\x8C\xE6\x8D\xA2"
    "\xE8\xA1\x8C\xE6\x97\xB6\xE5\xB9\xB3\xE6\xBB\x91\xE5\x9C\xB0\xE7\xA7\xBB\xE5\x8A\xA8\xE3\x80\x82",
    /* "每个字形都是独立的图层，换行时平滑地移动。" */
};

static void paragraph_text(size_t step, char *buf, size_t size, bool *down)
{
    snprintf(buf, size, "%s", paragraphs[step % (sizeof(paragraphs) / sizeof(*paragraphs))]);
    *down = false;
}

/* Transitions */

static gr_text_layout *layout_text(label *l, const char *utf8)
{
    gr_attributed_text *text =
        gr_attributed_text_new((gr_string){utf8, strlen(utf8)}, &l->style, (gr_text_span_list){NULL, 0});
    if (!text)
        abort();
    gr_text_layout *layout = gr_text_system_layout(l->system, text, &l->options);
    gr_attributed_text_unref(text);
    if (!layout)
        abort();
    return layout;
}

static void set_drawer(glyph_state *g, gr_text_layout *layout, size_t line, size_t index, gr_point origin,
                       gr_color color)
{
    glyph_drawing *data = malloc(sizeof(*data));
    if (!data)
        abort();
    *data = (glyph_drawing){gr_text_layout_ref(layout), line, index, origin, color};
    gr_drawer drawer = {data, draw_glyph, free_glyph};
    EX_OK(gr_content_layer_set_drawer(g->layer, &drawer));
}

static glyph_state *add_glyph(label *l)
{
    if (l->count == l->capacity)
    {
        l->capacity = l->capacity ? l->capacity * 2 : 32;
        void *p = realloc(l->glyphs, l->capacity * sizeof(*l->glyphs));
        if (!p)
            abort();
        l->glyphs = p;
    }
    glyph_state *g = &l->glyphs[l->count++];
    *g = (glyph_state){0};
    return g;
}

/* The glyph's layer covers its ink and its slice of the line box, so scaling centers on the line, rounded out to
 * whole points so the layer sits on the pixel grid while the glyph keeps its fractional position inside it. */
static gr_rect glyph_rect(const gr_text_glyph *glyph, const gr_text_line *line)
{
    double left = fmin(glyph->bounds.origin.x, glyph->position.x),
           right = fmax(glyph->bounds.origin.x + glyph->bounds.size.width, glyph->position.x + glyph->advance),
           top = fmin(glyph->bounds.origin.y, line->bounds.origin.y),
           bottom = fmax(glyph->bounds.origin.y + glyph->bounds.size.height,
                         line->bounds.origin.y + line->bounds.size.height);
    left = floor(left - 1);
    top = floor(top - 1);
    return EX_RECT(left, top, ceil(right + 1) - left, ceil(bottom + 1) - top);
}

static int compare_x(const void *a, const void *b)
{
    double x = (*(glyph_state *const *)a)->x.value, y = (*(glyph_state *const *)b)->x.value;
    return (x > y) - (x < y);
}

/* A visible glyph of the new text. */
typedef struct
{
    uint64_t font;
    uint32_t glyph;
    double font_size;
    size_t line, index;
    gr_rect rect;
} placed_glyph;

static bool same_shape(const glyph_state *g, const placed_glyph *p)
{
    return g->font == p->font && g->glyph == p->glyph && g->font_size == p->font_size;
}

static int compare_order(const void *a, const void *b)
{
    size_t x = (*(glyph_state *const *)a)->order, y = (*(glyph_state *const *)b)->order;
    return (x > y) - (x < y);
}

static void *checked_calloc(size_t count, size_t size)
{
    void *p = calloc(count ? count : 1, size);
    if (!p)
        abort();
    return p;
}

/* Lists the glyphs with ink, in reading order, skipping blank ones (spaces), which need no layer. */
static placed_glyph *place_glyphs(gr_text_layout *layout, size_t *out_count)
{
    size_t count = 0, capacity = 0;
    placed_glyph *placed = NULL;
    size_t lines = gr_text_layout_get_metrics(layout).line_count;
    for (size_t line_index = 0; line_index < lines; ++line_index)
    {
        gr_text_line line;
        EX_OK(gr_text_layout_get_line(layout, line_index, &line));
        size_t n = gr_text_layout_get_glyphs(layout, line_index, NULL, 0);
        gr_text_glyph *glyphs = checked_calloc(n, sizeof(*glyphs));
        gr_text_layout_get_glyphs(layout, line_index, glyphs, n);
        for (size_t i = 0; i < n; ++i)
        {
            const gr_text_glyph *g = &glyphs[i];
            if (g->bounds.size.width <= 0 || g->bounds.size.height <= 0)
                continue;
            if (count == capacity)
            {
                capacity = capacity ? capacity * 2 : 64;
                void *p = realloc(placed, capacity * sizeof(*placed));
                if (!p)
                    abort();
                placed = p;
            }
            placed[count++] = (placed_glyph){g->font, g->glyph, g->font_size, line_index, i, glyph_rect(g, &line)};
        }
        free(glyphs);
    }
    *out_count = count;
    return placed;
}

/* Pairs new glyphs with staying ones (sorted by order): the longest common subsequence of shapes first, so an edit
 * keeps the untouched text in place and reflows it, then any remaining new glyph takes the earliest unpaired staying
 * glyph of its shape, so repeated digits still roll into each other. match[i] is a staying index or SIZE_MAX. */
static void match_glyphs(glyph_state **staying, size_t old_count, const placed_glyph *placed, size_t new_count,
                         size_t *match)
{
    size_t width = new_count + 1;
    uint16_t *table = checked_calloc((old_count + 1) * width, sizeof(*table));
    for (size_t i = old_count; i-- > 0;)
        for (size_t j = new_count; j-- > 0;)
        {
            uint16_t skip_old = table[(i + 1) * width + j], skip_new = table[i * width + j + 1];
            table[i * width + j] = same_shape(staying[i], &placed[j]) ? (uint16_t)(table[(i + 1) * width + j + 1] + 1)
                                   : skip_old > skip_new              ? skip_old
                                                                      : skip_new;
        }
    bool *paired = checked_calloc(old_count, sizeof(*paired));
    for (size_t j = 0; j < new_count; ++j)
        match[j] = SIZE_MAX;
    for (size_t i = 0, j = 0; i < old_count && j < new_count;)
    {
        if (same_shape(staying[i], &placed[j]) && table[i * width + j] == table[(i + 1) * width + j + 1] + 1)
        {
            paired[i] = true;
            match[j++] = i++;
        }
        else if (table[(i + 1) * width + j] >= table[i * width + j + 1])
            ++i;
        else
            ++j;
    }
    for (size_t j = 0; j < new_count; ++j)
        for (size_t i = 0; match[j] == SIZE_MAX && i < old_count; ++i)
            if (!paired[i] && same_shape(staying[i], &placed[j]))
            {
                paired[i] = true;
                match[j] = i;
            }
    free(paired);
    free(table);
}

static void set_text(label *l, const char *utf8, bool down)
{
    gr_text_layout *layout = layout_text(l, utf8);
    size_t new_count, old_count = 0;
    placed_glyph *placed = place_glyphs(layout, &new_count);

    /* Glyphs already leaving keep leaving; the rest may stay. Indices, since adding glyphs moves the array. */
    glyph_state **staying = checked_calloc(l->count, sizeof(*staying));
    for (size_t i = 0; i < l->count; ++i)
    {
        l->glyphs[i].claimed = false;
        if (!l->glyphs[i].leaving)
            staying[old_count++] = &l->glyphs[i];
    }
    qsort(staying, old_count, sizeof(*staying), compare_order);
    size_t *match = checked_calloc(new_count, sizeof(*match));
    match_glyphs(staying, old_count, placed, new_count, match);
    size_t *targets = checked_calloc(new_count, sizeof(*targets));
    for (size_t j = 0; j < new_count; ++j)
        targets[j] = match[j] == SIZE_MAX ? SIZE_MAX : (size_t)(staying[match[j]] - l->glyphs);
    free(staying);

    size_t appearing_count = 0;
    size_t *appearing = checked_calloc(new_count, sizeof(*appearing));
    for (size_t j = 0; j < new_count; ++j)
    {
        const placed_glyph *p = &placed[j];
        gr_rect rect = p->rect;
        glyph_state *g;
        if (targets[j] != SIZE_MAX)
        {
            g = &l->glyphs[targets[j]];
            spring_to(&g->x, rect.origin.x);
            spring_to(&g->y, rect.origin.y);
        }
        else
        {
            g = add_glyph(l);
            g->font = p->font;
            g->glyph = p->glyph;
            g->font_size = p->font_size;
            g->layer = gr_layer_new(l->scene, GR_LAYER_CONTENT);
            if (!g->layer)
                abort();
            EX_OK(gr_layer_add_child(l->layer, g->layer));
            spring_set(&g->x, rect.origin.x, rect.origin.x);
            spring_set(&g->y, rect.origin.y, rect.origin.y);
            appearing[appearing_count++] = (size_t)(g - l->glyphs);
        }
        g->claimed = true;
        g->order = j;
        g->size = rect.size;
        EX_OK(gr_layer_set_bounds(g->layer, EX_RECT(0, 0, rect.size.width, rect.size.height)));
        set_drawer(g, layout, p->line, p->index, EX_POINT(-rect.origin.x, -rect.origin.y), l->color);
    }
    free(targets);
    free(match);
    free(placed);
    gr_text_layout_unref(layout);

    /* Glyphs not reused leave, left to right. */
    size_t leaving_count = 0;
    glyph_state **leaving = malloc((l->count ? l->count : 1) * sizeof(*leaving));
    if (!leaving)
        abort();
    for (size_t i = 0; i < l->count; ++i)
        if (!l->glyphs[i].leaving && !l->glyphs[i].claimed)
            leaving[leaving_count++] = &l->glyphs[i];
    qsort(leaving, leaving_count, sizeof(*leaving), compare_x);

    size_t most = appearing_count > leaving_count ? appearing_count : leaving_count;
    double stagger = most ? 0.2 / (double)most : 0;
    double from = down ? -1 : 1;
    for (size_t i = 0; i < appearing_count; ++i)
    {
        glyph_state *g = &l->glyphs[appearing[i]];
        g->delay = (double)i * stagger;
        spring_set(&g->scale, 0.4, 1);
        spring_set(&g->offset, from, 0);
        spring_set(&g->opacity, 0, 1);
        spring_set(&g->blur, log(g->size.height) / log(3), 0);
    }
    for (size_t i = 0; i < leaving_count; ++i)
    {
        glyph_state *g = leaving[i];
        bool moving =
            g->x.active || g->y.active || g->scale.active || g->offset.active || g->opacity.active || g->blur.active;
        if (!moving)
            g->delay = (double)i * stagger;
        g->leaving = true;
        spring_to(&g->scale, 0.4);
        spring_to(&g->offset, -from);
        spring_to(&g->opacity, 0);
        spring_to(&g->blur, log(g->size.height));
    }
    free(leaving);
    free(appearing);
}

static void advance_label(label *l, double dt)
{
    for (size_t i = 0; i < l->count; ++i)
    {
        glyph_state *g = &l->glyphs[i];
        if (g->delay > 0)
        {
            g->delay -= dt;
            continue;
        }
        spring_step(&g->x, MOVE, dt);
        spring_step(&g->y, MOVE, dt);
        spring_step(&g->scale, SCALE, dt);
        spring_step(&g->offset, OFFSET, dt);
        spring_step(&g->opacity, FADE, dt);
        spring_step(&g->blur, BLUR, dt);
    }
}

/* Writes the springs to the layers and drops glyphs that have faded out. */
static void pose_label(label *l)
{
    size_t kept = 0;
    for (size_t i = 0; i < l->count; ++i)
    {
        glyph_state *g = &l->glyphs[i];
        if (g->leaving && g->opacity.value < 0.01)
        {
            EX_OK(gr_layer_remove_from_parent(g->layer));
            gr_layer_unref(g->layer);
            continue;
        }
        double scale = g->scale.value;
        EX_OK(
            gr_layer_set_position(g->layer, EX_POINT(g->x.value + g->size.width / 2, g->y.value + g->size.height / 2)));
        EX_OK(
            gr_layer_set_transform(g->layer, (gr_affine){scale, 0, 0, scale, 0, g->offset.value * g->size.height / 3}));
        EX_OK(gr_layer_set_opacity(g->layer, (float)fmin(1, fmax(0, g->opacity.value))));
        EX_OK(gr_layer_set_blur(g->layer, fmax(0, g->blur.value)));
        l->glyphs[kept++] = *g;
    }
    l->count = kept;
}

static void next_text(label *l)
{
    char buf[512];
    bool down = false;
    l->text(l->step++, buf, sizeof(buf), &down);
    set_text(l, buf, down);
}

/* Scene */

static gr_layer *card(example *ex, size_t col, size_t row)
{
    gr_layer *c = pill(
        ex, gr_scene_get_root(ex->scene),
        EX_RECT(26 + (double)col * (CARD_WIDTH + 28), 26 + (double)row * (CARD_HEIGHT + 28), CARD_WIDTH, CARD_HEIGHT),
        EX_WHITE, 20);
    EX_OK(gr_layer_set_masks_to_bounds(c, true));
    border(c, 1, alpha(EX_BLACK, 0.06f));
    drop_shadow(c, alpha(EX_BLACK, 0.12f), 0, 6, 10, NULL);
    return c;
}

typedef struct
{
    gr_text_layout *layout;
    gr_color color;
} caption_drawing;

static void draw_caption(void *userdata, gr_canvas *canvas, const gr_draw_info *info)
{
    (void)info;
    const caption_drawing *c = userdata;
    EX_OK(gr_canvas_draw_text_layout(canvas, c->layout, EX_POINT(0, 0), c->color));
}

static void free_caption(void *userdata)
{
    caption_drawing *c = userdata;
    gr_text_layout_unref(c->layout);
    free(c);
}

static void caption(example *ex, gr_layer *parent, const char *utf8)
{
    gr_text_style style = gr_text_style_default();
    style.size = 13;
    style.weight = 600;
    gr_attributed_text *text =
        gr_attributed_text_new((gr_string){utf8, strlen(utf8)}, &style, (gr_text_span_list){NULL, 0});
    gr_text_layout_options options = gr_text_layout_options_default();
    gr_text_layout *layout = gr_text_system_layout(ex->text, text, &options);
    gr_attributed_text_unref(text);
    if (!layout)
        abort();
    gr_size size = gr_text_layout_get_metrics(layout).size;
    caption_drawing *data = malloc(sizeof(*data));
    if (!data)
        abort();
    *data = (caption_drawing){layout, alpha(hex(EX_INK), 0.45f)};
    gr_layer *l = new_layer(ex, GR_LAYER_CONTENT);
    EX_OK(gr_layer_set_frame(l, EX_RECT(INSET, 16, ceil(size.width), ceil(size.height))));
    gr_drawer drawer = {data, draw_caption, free_caption};
    EX_OK(gr_content_layer_set_drawer(l, &drawer));
    EX_OK(gr_layer_add_child(parent, l));
}

/* A label in a card. Single-line labels are centered vertically below the caption; wrapped ones start under it. */
static void label_init(glyphs_scene *d, label *l, size_t col, size_t row, const char *title, double size,
                       uint16_t weight, gr_text_align align, bool wrap, uint32_t color, label_text_fn text,
                       double period)
{
    example *ex = d->ex;
    gr_layer *c = card(ex, col, row);
    caption(ex, c, title);
    *l = (label){.scene = ex->scene, .system = ex->text, .color = hex(color), .text = text, .period = period};
    l->style = gr_text_style_default();
    l->style.size = size;
    l->style.weight = weight;
    l->options = gr_text_layout_options_default();
    l->options.has_max_width = true;
    l->options.max_width = LABEL_WIDTH;
    l->options.align = align;
    l->options.wrap = wrap ? GR_TEXT_WRAP_WORD : GR_TEXT_WRAP_NONE;
    double top = 52;
    if (!wrap)
    {
        gr_text_layout *probe = layout_text(l, "0");
        double height = gr_text_layout_get_metrics(probe).size.height;
        gr_text_layout_unref(probe);
        top = round((CARD_HEIGHT + 36 - height) / 2);
    }
    l->layer = layer(ex, c, EX_RECT(INSET, top, LABEL_WIDTH, CARD_HEIGHT - top));
    l->next_change = period;
    next_text(l);
}

static void *build(example *ex)
{
    glyphs_scene *d = calloc(1, sizeof(*d));
    if (!d)
        abort();
    d->ex = ex;
    gr_layer *backdrop = new_layer(ex, GR_LAYER_GRADIENT);
    EX_OK(gr_layer_set_frame(backdrop, EX_RECT(0, 0, 960, 600)));
    gr_gradient_stop stops[] = {{0, hex(0xEEF1F8)}, {1, hex(0xF6EEF7)}};
    set_gradient(backdrop, gradient(false, EX_POINT(0, 0), EX_POINT(1, 1), 0, (gr_gradient_stop_list){stops, 2}));
    EX_OK(gr_layer_add_child(gr_scene_get_root(ex->scene), backdrop));
    if (!ex->text)
        return d; /* Nothing to animate without a text system. */
    label_init(d, &d->labels[0], 0, 0, "Clock", 96, 700, GR_TEXT_ALIGN_CENTER, false, EX_INK, clock_text, 1.0);
    label_init(d, &d->labels[1], 1, 0, "Counter", 56, 700, GR_TEXT_ALIGN_CENTER, false, EX_BLUE, counter_text, 1.7);
    label_init(d, &d->labels[2], 0, 1, "Phrases", 44, 600, GR_TEXT_ALIGN_CENTER, false, EX_PURPLE, phrase_text, 2.2);
    label_init(d, &d->labels[3], 1, 1, "Paragraph", 22, 500, GR_TEXT_ALIGN_START, true, EX_INK, paragraph_text, 3.0);
    return d;
}

static void update(void *state, double time)
{
    glyphs_scene *d = state;
    if (!d->ex->text)
        return;
    while (d->time < time)
    {
        double dt = fmin(STEP, time - d->time);
        for (size_t i = 0; i < LABELS; ++i)
        {
            label *l = &d->labels[i];
            if (l->next_change - d->time < dt)
                dt = fmax(0, l->next_change - d->time);
        }
        for (size_t i = 0; i < LABELS; ++i)
            advance_label(&d->labels[i], dt);
        d->time += dt;
        for (size_t i = 0; i < LABELS; ++i)
        {
            label *l = &d->labels[i];
            if (d->time >= l->next_change)
            {
                l->next_change += l->period;
                next_text(l);
            }
        }
    }
    for (size_t i = 0; i < LABELS; ++i)
        pose_label(&d->labels[i]);
}

static void free_state(void *state)
{
    glyphs_scene *d = state;
    for (size_t i = 0; i < LABELS; ++i)
    {
        label *l = &d->labels[i];
        for (size_t j = 0; j < l->count; ++j)
            gr_layer_unref(l->glyphs[j].layer);
        free(l->glyphs);
    }
    free(d);
}

int main(int argc, char **argv)
{
    static const example_desc desc = {"glyphs", {960, 600}, build, update, free_state, NULL};
    return example_main(argc, argv, &desc);
}
