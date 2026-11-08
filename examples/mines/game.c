/* The board: tiles that dent around a press, open in ripples, and mines that go off one after another, faster and
 * faster. Cells only animate while they are on the active list. */
#include "mines.h"

#include <time.h>

static const uint32_t tints[9] = {C_OPEN,   0xE1EED8, 0xEEEACF, 0xF3E0CE, 0xF0D8DB,
                                  0xE4DEEF, 0xD8EAEA, 0xE2E2DF, 0xE6E4DE};
static const uint32_t inks[9] = {C_INK, 0x4E7F4A, 0x86733A, 0xAA6540, 0xA24C62, 0x6D58A0, 0x3E7D80, 0x4C4C4C, 0x7A7A78};
static const uint32_t confetti[] = {0x9CC59A, 0xE8D48A, 0xF0B48E, 0xE9A1AE, 0xB7A6DA, 0x8CC7C9, C_ACCENT, C_FLAG};

static const spring_params HOVER = {0.22, 1}, POP = {0.42, 0.46}, OPENING = {0.26, 1}, BOOM = {0.9, 1},
                           GLYPH = {0.36, 0.52}, FADE = {0.2, 1}, KICK = {0.46, 0.3}, DEPTH_IN = {0.14, 1},
                           DEPTH_OUT = {0.5, 0.34}, FOLLOW = {0.1, 1}, CARD = {0.42, 0.78}, CHIP = {0.32, 0.36};

#define PAD 10.0
#define RING_R 50.0
#define CARD_W 340.0
#define CARD_H 232.0

/* Helpers */

static double cell_x(const game_page *g, int i)
{
    return (i % g->cols + 0.5) * g->pitch;
}

static double cell_y(const game_page *g, int i)
{
    return (i / g->cols + 0.5) * g->pitch;
}

/* Distance between two cells in cells. */
static double cell_dist(const game_page *g, int a, int b)
{
    return hypot(a % g->cols - b % g->cols, a / g->cols - b / g->cols);
}

static uint64_t rnd(game_page *g)
{
    g->seed ^= g->seed >> 12;
    g->seed ^= g->seed << 25;
    g->seed ^= g->seed >> 27;
    return g->seed * 2685821657736338717ull;
}

static double frand(game_page *g)
{
    return (double)(rnd(g) >> 11) * (1.0 / 9007199254740992.0);
}

static void activate(game_page *g, int i)
{
    cell *k = &g->cells[i];
    if (k->active)
        return;
    k->active = true;
    g->active[g->active_count++] = i;
}

static bool due(double *at, double now)
{
    if (*at < 0 || now < *at)
        return false;
    *at = -1;
    return true;
}

/* Fills out with the indices of i's neighbors and returns how many there are. */
static int neighbors(const game_page *g, int i, int out[8])
{
    int n = 0, c0 = i % g->cols, r0 = i / g->cols;
    for (int r = r0 - 1; r <= r0 + 1; ++r)
        for (int c = c0 - 1; c <= c0 + 1; ++c)
            if (c >= 0 && r >= 0 && c < g->cols && r < g->rows && (c != c0 || r != r0))
                out[n++] = r * g->cols + c;
    return n;
}

/* Glyphs */

static void draw_glyph(void *userdata, gr_canvas *canvas, const gr_draw_info *info)
{
    const cell *k = userdata;
    gr_rect b = info->bounds;
    double s = b.size.width;
    switch (k->shown)
    {
        case SHOW_NUMBER:
        {
            char value[2] = {(char)('0' + k->digit), 0};
            draw_centered_text(canvas, k->game->ex->text, value, b, s * 0.56, 700, hex(inks[k->digit]));
            break;
        }
        case SHOW_FLAG:
            draw_icon(canvas, ICON_FLAG, inset(b, s * 0.17), hex(C_FLAG));
            break;
        case SHOW_MINE:
            draw_icon(canvas, ICON_MINE, inset(b, s * 0.15), hex(C_SLATE));
            break;
        case SHOW_WRONG:
            draw_icon(canvas, ICON_WRONG, inset(b, s * 0.17), hex(C_BOOM));
            break;
        default:
            break;
    }
}

static void show(game_page *g, cell *k, uint8_t shown)
{
    if (!k->glyph)
    {
        k->glyph = new_layer(g->ex, GR_LAYER_CONTENT);
        EX_OK(gr_layer_set_frame(k->glyph, EX_RECT(0, 0, g->tile, g->tile)));
        gr_drawer drawer = {k, draw_glyph, NULL};
        EX_OK(gr_content_layer_set_drawer(k->glyph, &drawer));
        EX_OK(gr_layer_add_child(k->tile, k->glyph));
    }
    uint8_t digit = shown == SHOW_NUMBER ? k->adjacent : 0;
    if (k->shown != shown || k->digit != digit)
    {
        k->shown = shown;
        k->digit = digit;
        EX_OK(gr_content_layer_set_needs_display(k->glyph));
    }
}

/* Effects */

static void spawn_ring(game_page *g, double x, double y, double strength)
{
    ring *r = &g->rings[g->next_ring++ % RINGS];
    r->start = g->now;
    r->size = (g->pitch * 2.6 + 36) * (0.55 + 0.45 * strength) / RING_R;
    r->live = true;
    EX_OK(gr_layer_set_frame(r->layer, EX_RECT(x - RING_R, y - RING_R, 2 * RING_R, 2 * RING_R)));
    EX_OK(gr_layer_set_transform(r->layer, scaling(0.05)));
    EX_OK(gr_layer_set_hidden(r->layer, false));
}

static void launch(game_page *g, bit *b, gr_point p, gr_point v, double delay, double life, double gravity, double drag,
                   gr_color color, double size)
{
    *b = (bit){b->layer, p, v, frand(g) * EX_TAU, (frand(g) - 0.5) * 18, g->now + delay, life, gravity, drag, true};
    EX_OK(gr_layer_set_frame(b->layer, EX_RECT(p.x - size / 2, p.y - size * 0.35, size, size * 0.7)));
    EX_OK(gr_layer_set_corner_radius(b->layer, size * 0.2));
    background(b->layer, color);
    EX_OK(gr_layer_set_opacity(b->layer, 1));
    EX_OK(gr_layer_set_hidden(b->layer, delay > 0));
}

static void spawn_bit(game_page *g, gr_point p, gr_point v, double life, double gravity, double drag, gr_color color,
                      double size)
{
    launch(g, &g->bits[g->next_bit++ % BITS], p, v, 0, life, gravity, drag, color, size);
}

static void spawn_debris(game_page *g, double x, double y, double strength)
{
    static const uint32_t colors[] = {C_SLATE, C_BOOM, 0xD9CBB3, C_BOOM};
    int n = (int)(2 + 6 * strength);
    double scale = clampd(g->pitch / 36, 0.5, 1.3);
    for (int i = 0; i < n; ++i)
    {
        double a = frand(g) * EX_TAU, speed = (140 + frand(g) * 260) * (0.5 + 0.5 * strength) * scale;
        gr_point v = {cos(a) * speed, sin(a) * speed - 160 * scale};
        spawn_bit(g, EX_POINT(x, y), v, 0.5 + frand(g) * 0.4, 1100 * scale, 1.4, hex(colors[i % 4]),
                  (3 + frand(g) * 3.5) * scale);
    }
}

/* Win confetti fills the window: two cannons fire up from the bottom corners while more drifts down from the top. */
static void celebrate(game_page *g)
{
    double w = g->view.width, h = g->view.height, reach = sqrt(h / 720), size = clampd(fmin(w, h) / 640, 0.55, 1);
    int colors = (int)(sizeof(confetti) / sizeof(confetti[0])), n = 0;
    for (int i = 0; i < 160; ++i, ++n)
    {
        bool left = i % 2 == 0;
        double a = -EX_PI / 2 + (left ? 1 : -1) * (0.32 + frand(g) * 0.42), speed = (1500 + frand(g) * 900) * reach;
        gr_point v = {cos(a) * speed, sin(a) * speed};
        launch(g, &g->flakes[g->next_flake++ % FLAKES], EX_POINT(left ? -8 : w + 8, h + 8), v, frand(g) * 0.25,
               2.6 + frand(g) * 1.2, 900 * reach, 1.5, hex(confetti[n % colors]), (7 + frand(g) * 5) * size);
    }
    for (int i = 0; i < 100; ++i, ++n)
    {
        gr_point v = {(frand(g) - 0.5) * 80, 40 + frand(g) * 120};
        double life = (h + 60) / 230 + 0.4 + frand(g);
        launch(g, &g->flakes[g->next_flake++ % FLAKES], EX_POINT(frand(g) * w, -16 - frand(g) * 40), v,
               0.35 + frand(g) * 1.3, life, 210, 0.9, hex(confetti[n % colors]), (7 + frand(g) * 4) * size);
    }
}

/* Cell visuals */

static double press_radius(const game_page *g)
{
    return fmax(g->pitch * 2.6, 72);
}

/* How deep the dent around the press reaches this point: a smooth bowl, negative while it springs back up. */
static double press_factor(const game_page *g, double x, double y)
{
    double depth = g->depth.value;
    if (fabs(depth) < 1e-4)
        return 0;
    double r = press_radius(g), dx = x - g->cx.value, dy = y - g->cy.value, d2 = (dx * dx + dy * dy) / (r * r);
    if (d2 >= 1)
        return 0;
    return (1 - d2) * (1 - d2) * depth;
}

static void apply(game_page *g, int i)
{
    cell *k = &g->cells[i];
    /* Opened tiles are solid ground: they don't sink with the dent. */
    double x = cell_x(g, i), y = cell_y(g, i), pf = press_factor(g, x, y) * (1 - clampd(k->open.value, 0, 1));
    double s = fmax(0, k->pop.value * (1 + 0.07 * k->hover.value) * (1 - 0.2 * pf));
    gr_affine t = scaling(s);
    /* Tiles in the dent lean toward its center, as if the surface sank under the finger. */
    t.tx = k->kx.value + (g->cx.value - x) * 0.11 * pf;
    t.ty = k->ky.value + (g->cy.value - y) * 0.11 * pf;
    EX_OK(gr_layer_set_transform(k->tile, t));
    gr_color c = mix(hex(C_HIDDEN), hex(C_HIDDEN_HOVER), k->hover.value);
    c = mix(c, hex(k->mine ? C_MINE_OPEN : tints[k->adjacent]), k->open.value);
    c = mix(c, hex(C_BOOM), k->boom.value);
    c = mix(c, hex(0x949C92), 0.3 * fmax(0, pf));
    background(k->tile, c);
    if (k->glyph)
    {
        EX_OK(gr_layer_set_opacity(k->glyph, (float)clampd(k->galpha.value, 0, 1)));
        EX_OK(gr_layer_set_transform(k->glyph, scaling(fmax(0, k->gscale.value))));
    }
}

static void reset_visual(cell *k)
{
    k->reset_at = -1;
    spring_to(&k->open, 0);
    spring_to(&k->boom, 0);
    spring_to(&k->galpha, 0);
    spring_to(&k->gscale, 0.5);
}

static void reveal_visual(game_page *g, cell *k)
{
    spring_to(&k->open, 1);
    spring_to(&k->hover, 0);
    spring_set(&k->pop, fmin(k->pop.value, 0.7), 1);
    if (k->adjacent)
    {
        show(g, k, SHOW_NUMBER);
        spring_set(&k->gscale, 0.3, 1);
        spring_set(&k->galpha, 0, 1);
    }
    else
        spring_to(&k->galpha, 0);
}

static void explode(game_page *g, int i)
{
    cell *k = &g->cells[i];
    double strength = fmax(0.28, pow(0.9, k->rank));
    spring_set(&k->open, 1, 1);
    spring_set(&k->boom, 1, k->first_boom ? 0.85 : 0);
    spring_set(&k->pop, 1 + 0.45 * strength, 1);
    show(g, k, SHOW_MINE);
    spring_set(&k->gscale, 0.2, 1);
    spring_set(&k->galpha, 1, 1);
    double x = cell_x(g, i), y = cell_y(g, i);
    if (k->first_boom)
        spawn_ring(g, x, y, strength);
    spawn_debris(g, x, y, strength);
    /* Neighbors get shoved away and squashed, which spreads each blast through the board. */
    const double reach = 3.2;
    int c0 = i % g->cols, r0 = i / g->cols;
    for (int r = r0 - 3; r <= r0 + 3; ++r)
        for (int c = c0 - 3; c <= c0 + 3; ++c)
        {
            if (c < 0 || r < 0 || c >= g->cols || r >= g->rows || (c == c0 && r == r0))
                continue;
            double d = hypot(c - c0, r - r0);
            if (d >= reach)
                continue;
            double f = pow(1 - d / reach, 2) * strength;
            cell *n = &g->cells[r * g->cols + c];
            spring_kick(&n->kx, (c - c0) / d * f * g->pitch * 7);
            spring_kick(&n->ky, (r - r0) / d * f * g->pitch * 7);
            spring_kick(&n->pop, -2.6 * f);
            activate(g, r * g->cols + c);
        }
    g->shake = fmin(g->shake + 7 * strength, 10);
}

static void wave_visual(game_page *g, cell *k)
{
    if (g->play == PLAY_WON)
    {
        spring_kick(&k->pop, 2.4);
        if (k->mine && k->state != FLAGGED)
        {
            k->state = FLAGGED;
            show(g, k, SHOW_FLAG);
            spring_set(&k->gscale, 1.9, 1);
            spring_set(&k->galpha, 0, 1);
        }
    }
    else if (k->state == FLAGGED && !k->mine)
    {
        show(g, k, SHOW_WRONG);
        spring_set(&k->gscale, 1.6, 1);
        spring_set(&k->galpha, 0, 1);
        spring_kick(&k->pop, -2.2);
    }
}

/* Rules */

static void place(game_page *g, int first)
{
    int n = g->cols * g->rows, c0 = first % g->cols, r0 = first / g->cols, free_count = 0;
    bool roomy = n - 9 >= g->count;
    for (int i = 0; i < n; ++i)
    {
        int c = i % g->cols, r = i / g->cols;
        bool near = roomy ? abs(c - c0) <= 1 && abs(r - r0) <= 1 : i == first;
        if (!near)
            g->queue[free_count++] = i;
    }
    for (int j = 0; j < g->count && j < free_count; ++j)
    {
        int pick = j + (int)(rnd(g) % (uint64_t)(free_count - j)), t = g->queue[j];
        g->queue[j] = g->queue[pick];
        g->queue[pick] = t;
        g->cells[g->queue[j]].mine = 1;
    }
    for (int i = 0; i < n; ++i)
    {
        int around[8], count = 0, n_around = neighbors(g, i, around);
        for (int q = 0; q < n_around; ++q)
            count += g->cells[around[q]].mine;
        g->cells[i].adjacent = (uint8_t)count;
    }
}

/* Opens start and, through empty cells, everything around it; each opens later the farther it is from origin. */
static void open_from(game_page *g, int start, int origin)
{
    if (g->cells[start].state != HIDDEN)
        return;
    int head = 0, tail = 0;
    g->queue[tail++] = start;
    g->cells[start].state = OPEN;
    while (head < tail)
    {
        int i = g->queue[head++];
        cell *k = &g->cells[i];
        double d = cell_dist(g, i, origin);
        if (k->reset_at >= 0)
            reset_visual(k);
        k->reveal_at = g->now + (d <= 30 ? d * 0.03 : 0.9 + (d - 30) * 0.012);
        activate(g, i);
        ++g->opened;
        if (k->mine || k->adjacent)
            continue;
        int around[8], n_around = neighbors(g, i, around);
        for (int q = 0; q < n_around; ++q)
            if (g->cells[around[q]].state == HIDDEN)
            {
                g->cells[around[q]].state = OPEN;
                g->queue[tail++] = around[q];
            }
    }
}

typedef struct
{
    int index;
    double d;
} ranked;

static int by_distance(const void *a, const void *b)
{
    double x = ((const ranked *)a)->d, y = ((const ranked *)b)->d;
    return (x > y) - (x < y);
}

static void lose(game_page *g, int hit)
{
    g->play = PLAY_LOST;
    g->ended = g->now;
    cell *h = &g->cells[hit];
    h->state = OPEN;
    h->first_boom = true;
    h->rank = 0;
    h->explode_at = g->now;
    activate(g, hit);

    /* The rest go off nearest first: one by one at first, then faster and faster. */
    int n = g->cols * g->rows, count = 0;
    ranked *order = malloc(sizeof(*order) * (size_t)g->count);
    if (!order)
        abort();
    for (int i = 0; i < n; ++i)
        if (g->cells[i].mine && i != hit && g->cells[i].state != FLAGGED)
            order[count++] = (ranked){i, cell_dist(g, i, hit)};
    qsort(order, (size_t)count, sizeof(*order), by_distance);
    double t = g->now + 0.45, gap = 0.26;
    for (int k = 0; k < count; ++k)
    {
        cell *c = &g->cells[order[k].index];
        c->rank = (uint16_t)(k + 1 < 65535 ? k + 1 : 65535);
        c->explode_at = t;
        activate(g, order[k].index);
        t += gap;
        gap = fmax(0.0025, gap * 0.84);
    }
    free(order);
    for (int i = 0; i < n; ++i)
        if (g->cells[i].state == FLAGGED && !g->cells[i].mine)
        {
            g->cells[i].wave_at = t + 0.1;
            activate(g, i);
        }
    g->card_at = t + 0.75;
}

static void win(game_page *g, int origin)
{
    g->play = PLAY_WON;
    g->ended = g->now;
    g->flags = g->count;
    spring_kick(&g->flag_pop, 6);
    int n = g->cols * g->rows;
    double far = 0;
    for (int i = 0; i < n; ++i)
        far = fmax(far, cell_dist(g, i, origin));
    double spread = fmin(0.03, 1.1 / fmax(far, 1));
    for (int i = 0; i < n; ++i)
    {
        g->cells[i].wave_at = g->now + 0.2 + cell_dist(g, i, origin) * spread;
        activate(g, i);
    }
    celebrate(g);
    g->card_at = g->now + 0.2 + far * spread + 0.5;
}

static void check_win(game_page *g, int origin)
{
    if (g->play == PLAY_RUNNING && g->opened == g->cols * g->rows - g->count)
        win(g, origin);
}

/* Opens the closed neighbors of an opened number once its flags match the number, or nudges them when they don't. */
static void chord(game_page *g, int i)
{
    cell *k = &g->cells[i];
    if (g->play != PLAY_RUNNING || k->state != OPEN || !k->adjacent)
        return;
    int around[8], n_around = neighbors(g, i, around), flags = 0;
    for (int q = 0; q < n_around; ++q)
        flags += g->cells[around[q]].state == FLAGGED;
    if (flags != k->adjacent)
    {
        for (int q = 0; q < n_around; ++q)
            if (g->cells[around[q]].state == HIDDEN)
            {
                spring_kick(&g->cells[around[q]].pop, -1.8);
                activate(g, around[q]);
            }
        return;
    }
    int boom = -1;
    for (int q = 0; q < n_around; ++q)
    {
        int j = around[q];
        if (g->cells[j].state != HIDDEN)
            continue;
        if (g->cells[j].mine)
            boom = boom < 0 ? j : boom;
        else
            open_from(g, j, i);
    }
    if (boom >= 0)
    {
        g->last = boom;
        lose(g, boom);
    }
    else
        check_win(g, i);
}

static void reveal(game_page *g, int i)
{
    cell *k = &g->cells[i];
    if ((g->play != PLAY_READY && g->play != PLAY_RUNNING) || k->state == OPEN)
        return;
    if (k->state == FLAGGED)
    {
        spring_kick(&k->kx, g->pitch * 4);
        activate(g, i);
        return;
    }
    if (g->play == PLAY_READY)
    {
        place(g, i);
        g->play = PLAY_RUNNING;
        g->started = g->now;
    }
    g->last = i;
    if (k->mine)
    {
        if (k->reset_at >= 0)
            reset_visual(k);
        lose(g, i);
        return;
    }
    open_from(g, i, i);
    check_win(g, i);
}

static void flag(game_page *g, int i)
{
    cell *k = &g->cells[i];
    if (g->play != PLAY_READY && g->play != PLAY_RUNNING)
        return;
    if (k->state == HIDDEN)
    {
        if (k->reset_at >= 0)
            reset_visual(k);
        k->state = FLAGGED;
        ++g->flags;
        show(g, k, SHOW_FLAG);
        spring_set(&k->gscale, 1.9, 1);
        spring_set(&k->galpha, 0, 1);
        spring_kick(&k->pop, -1.4);
    }
    else if (k->state == FLAGGED)
    {
        k->state = HIDDEN;
        --g->flags;
        spring_to(&k->galpha, 0);
        spring_to(&k->gscale, 0.4);
        spring_kick(&k->pop, 1.2);
    }
    else
        return;
    spring_kick(&g->flag_pop, 5);
    activate(g, i);
}

/* Build and layout */

static void ensure_pool(game_page *g, int n)
{
    for (; g->pool < n; ++g->pool)
    {
        cell *k = &g->cells[g->pool];
        k->game = g;
        k->tile = layer(g->ex, g->tiles, EX_RECT(0, 0, 1, 1));
        background(k->tile, hex(C_HIDDEN));
    }
}

static gr_layer *chip(example *ex, gr_layer *parent)
{
    gr_layer *l = pill(ex, parent, EX_RECT(0, 0, 10, 40), hex(C_PANEL), 20);
    border(l, 1, hex(C_LINE));
    return l;
}

void game_build(mines *m)
{
    example *ex = m->ex;
    game_page *g = &m->game;
    g->ex = ex;
    g->cells = calloc(MAX_CELLS, sizeof(*g->cells));
    g->active = malloc(MAX_CELLS * sizeof(*g->active));
    g->queue = malloc(MAX_CELLS * sizeof(*g->queue));
    if (!g->cells || !g->active || !g->queue)
        abort();
    g->seed = 0x9E3779B97F4A7C15ull ^ (uint64_t)(uintptr_t)g ^ (uint64_t)time(NULL);
    g->hover = g->last = -1;
    g->card_at = -1;
    gr_layer *root = gr_scene_get_root(ex->scene);
    g->page = layer(ex, root, EX_RECT(0, 0, m->view.width, m->view.height));
    EX_OK(gr_layer_set_hidden(g->page, true));

    g->backdrop = layer(ex, g->page, EX_RECT(0, 0, m->view.width, m->view.height));
    g->panel = pill(ex, g->backdrop, EX_RECT(0, 0, 10, 10), hex(C_PANEL), 16);
    border(g->panel, 1, hex(C_LINE));
    drop_shadow(g->panel, alpha(hex(0x5A5446), 0.2f), 0, 10, 22, NULL);
    g->board = layer(ex, g->panel, EX_RECT(PAD, PAD, 10, 10));
    g->tiles = layer(ex, g->board, EX_RECT(0, 0, 10, 10));
    g->fx = layer(ex, g->board, EX_RECT(0, 0, 10, 10));
    for (int i = 0; i < RINGS; ++i)
    {
        ring *r = &g->rings[i];
        r->layer = shape(ex, g->fx, EX_RECT(0, 0, 2 * RING_R, 2 * RING_R));
        set_path(r->layer, circle(EX_POINT(RING_R, RING_R), RING_R - 2));
        shape_stroke(r->layer,
                     stroke(paint(alpha(hex(C_BOOM), 0.9f)), 2.5, GR_CAP_ROUND, GR_JOIN_ROUND, (gr_double_list){0}, 0));
        EX_OK(gr_layer_set_hidden(r->layer, true));
    }
    for (int i = 0; i < BITS; ++i)
    {
        g->bits[i].layer = pill(ex, g->fx, EX_RECT(0, 0, 6, 4), hex(C_SLATE), 1);
        EX_OK(gr_layer_set_hidden(g->bits[i].layer, true));
    }

    button_init(ex, &g->back, g->backdrop, "‹  Sizes", NULL, false);
    button_init(ex, &g->restart, g->backdrop, "New game", NULL, false);
    g->flag_chip = chip(ex, g->backdrop);
    g->flag_icon = icon_new(ex, g->flag_chip, ICON_FLAG, hex(C_FLAG));
    g->flag_text = label_new(ex, g->flag_chip, 16, 600, hex(C_INK), GR_TEXT_ALIGN_START, "0");
    g->time_chip = chip(ex, g->backdrop);
    g->clock_icon = icon_new(ex, g->time_chip, ICON_CLOCK, hex(C_SLATE));
    g->time_text = label_new(ex, g->time_chip, 16, 600, hex(C_INK), GR_TEXT_ALIGN_START, "00:00");

    g->veil = layer(ex, g->page, EX_RECT(0, 0, 10, 10));
    background(g->veil, hex(C_PAGE));
    EX_OK(gr_layer_set_hidden(g->veil, true));
    g->card = pill(ex, g->page, EX_RECT(0, 0, CARD_W, CARD_H), EX_WHITE, 22);
    border(g->card, 1, hex(C_LINE));
    drop_shadow(g->card, alpha(hex(0x4A4438), 0.16f), 0, 14, 28, NULL);
    EX_OK(gr_layer_set_hidden(g->card, true));
    g->card_flag = icon_new(ex, g->card, ICON_FLAG, hex(C_FLAG));
    g->card_mine = icon_new(ex, g->card, ICON_MINE, hex(C_SLATE));
    g->card_title = label_new(ex, g->card, 24, 700, hex(C_INK), GR_TEXT_ALIGN_CENTER, "");
    g->card_detail = label_new(ex, g->card, 13, 500, hex(C_MUTED), GR_TEXT_ALIGN_CENTER, "");
    button_init(ex, &g->again, g->card, "Play again", NULL, true);
    button_init(ex, &g->sizes, g->card, "Change size", NULL, false);

    g->sky = layer(ex, g->page, EX_RECT(0, 0, m->view.width, m->view.height));
    for (int i = 0; i < FLAKES; ++i)
    {
        g->flakes[i].layer = pill(ex, g->sky, EX_RECT(0, 0, 8, 6), hex(C_ACCENT), 1.5);
        EX_OK(gr_layer_set_hidden(g->flakes[i].layer, true));
    }
    g->card_scale = 1;
}

void game_layout(mines *m)
{
    game_page *g = &m->game;
    double w = m->view.width, h = m->view.height;
    EX_OK(gr_layer_set_frame(g->page, EX_RECT(0, 0, w, h)));

    g->view = m->view;
    EX_OK(gr_layer_set_frame(g->sky, EX_RECT(0, 0, w, h)));
    EX_OK(gr_layer_set_frame(g->backdrop, EX_RECT(0, 0, w, h)));
    /* Narrow windows get icon buttons, smaller chips and margins; the clock goes when even those don't fit. */
    bool compact = w < 560;
    double margin = compact ? 12 : 24, top = compact ? 12 : 18, hud = top + 40 + (compact ? 12 : 18);
    double bw0 = compact ? 40 : 96, rw0 = compact ? 40 : 116;
    g->back.title->size = compact ? 22 : 15;
    g->restart.title->size = compact ? 19 : 15;
    label_set(g->back.title, compact ? "‹" : "‹  Sizes");
    label_set(g->restart.title, compact ? "↻" : "New game");
    button_layout(&g->back, EX_RECT(margin, top, bw0, 40));
    button_layout(&g->restart, EX_RECT(w - margin - rw0, top, rw0, 40));
    double fw = compact ? 76 : 96, tw = compact ? 94 : 106, chip_gap = compact ? 8 : 10;
    bool clock = w - 2 * (margin + 40 + 8) >= fw + chip_gap + tw;
    double x0 = clock ? w / 2 - (fw + chip_gap + tw) / 2 : w / 2 - fw / 2;
    EX_OK(gr_layer_set_hidden(g->time_chip, !clock));
    EX_OK(gr_layer_set_frame(g->flag_chip, EX_RECT(x0, top, fw, 40)));
    EX_OK(gr_layer_set_frame(g->time_chip, EX_RECT(x0 + fw + chip_gap, top, tw, 40)));
    EX_OK(gr_layer_set_frame(g->flag_icon, EX_RECT(compact ? 11 : 14, 10, 20, 20)));
    EX_OK(gr_layer_set_frame(g->clock_icon, EX_RECT(compact ? 11 : 14, 11, 18, 18)));
    label_frame(g->flag_text, EX_RECT(compact ? 36 : 42, 0, fw - (compact ? 40 : 48), 40));
    label_frame(g->time_text, EX_RECT(compact ? 36 : 40, 0, tw - (compact ? 40 : 46), 40));

    /* The result card shrinks to fit. */
    g->card_scale = fmin(1, fmin((w - 2 * margin) / CARD_W, (h - 2 * margin) / CARD_H));
    EX_OK(gr_layer_set_frame(g->veil, EX_RECT(0, 0, w, h)));
    g->card_rect = EX_RECT(w / 2 - CARD_W / 2, h / 2 - CARD_H / 2, CARD_W, CARD_H);
    EX_OK(gr_layer_set_frame(g->card, g->card_rect));
    EX_OK(gr_layer_set_frame(g->card_flag, EX_RECT(CARD_W / 2 - 22, 22, 44, 44)));
    EX_OK(gr_layer_set_frame(g->card_mine, EX_RECT(CARD_W / 2 - 22, 22, 44, 44)));
    label_frame(g->card_title, EX_RECT(0, 74, CARD_W, 32));
    label_frame(g->card_detail, EX_RECT(0, 108, CARD_W, 20));
    /* Card buttons lay out in card space but hit-test in page space, through the card's scale. */
    double bwid = (CARD_W - 24 * 2 - 12) / 2, cs = g->card_scale;
    button_layout(&g->sizes, EX_RECT(24, CARD_H - 24 - 48, bwid, 48));
    button_layout(&g->again, EX_RECT(24 + bwid + 12, CARD_H - 24 - 48, bwid, 48));
    for (int i = 0; i < 2; ++i)
    {
        button *b = i ? &g->again : &g->sizes;
        b->rect = EX_RECT(w / 2 + (b->rect.origin.x - CARD_W / 2) * cs, h / 2 + (b->rect.origin.y - CARD_H / 2) * cs,
                          b->rect.size.width * cs, b->rect.size.height * cs);
    }

    if (!g->cols)
        return;
    double pad = compact ? 6 : PAD, aw = w - 2 * margin, ah = h - hud - margin;
    double pitch = fmin(fmin((aw - 2 * pad) / g->cols, (ah - 2 * pad) / g->rows), 54);
    g->pitch = pitch = fmax(pitch, 3);
    double bw = pitch * g->cols, bh = pitch * g->rows;
    g->board_rect = EX_RECT(w / 2 - bw / 2, hud + fmax(0, (ah - bh) / 2), bw, bh);
    EX_OK(gr_layer_set_frame(g->panel, inset(g->board_rect, -pad)));
    EX_OK(gr_layer_set_corner_radius(g->panel, fmin(16, pad + pitch * 0.2)));
    EX_OK(gr_layer_set_frame(g->board, EX_RECT(pad, pad, bw, bh)));
    EX_OK(gr_layer_set_frame(g->tiles, EX_RECT(0, 0, bw, bh)));
    EX_OK(gr_layer_set_frame(g->fx, EX_RECT(0, 0, bw, bh)));
    double gap = pitch >= 12 ? pitch * 0.09 : pitch >= 6 ? 1 : 0.5;
    g->tile = pitch - gap;
    int n = g->cols * g->rows;
    for (int i = 0; i < g->pool; ++i)
    {
        cell *k = &g->cells[i];
        EX_OK(gr_layer_set_hidden(k->tile, i >= n));
        if (i >= n)
            continue;
        EX_OK(gr_layer_set_frame(
            k->tile, EX_RECT((i % g->cols) * pitch + gap / 2, (i / g->cols) * pitch + gap / 2, g->tile, g->tile)));
        EX_OK(gr_layer_set_corner_radius(k->tile, g->tile * 0.2));
        if (k->glyph)
            EX_OK(gr_layer_set_frame(k->glyph, EX_RECT(0, 0, g->tile, g->tile)));
    }
}

void game_new(mines *m, int cols, int rows, int count, bool intro)
{
    game_page *g = &m->game;
    g->now = m->now;
    g->cols = cols;
    g->rows = rows;
    g->count = count;
    ensure_pool(g, cols * rows);
    g->play = PLAY_READY;
    g->flags = g->opened = 0;
    g->hover = g->last = -1;
    g->card_at = -1;
    spring_to(&g->card_t, 0);
    game_layout(m);

    int n = cols * rows;
    double mc = (cols - 1) / 2.0, mr = (rows - 1) / 2.0, spread = fmin(0.022, 0.7 / fmax(1, hypot(mc, mr)));
    for (int i = 0; i < n; ++i)
    {
        cell *k = &g->cells[i];
        k->mine = k->adjacent = 0;
        k->state = HIDDEN;
        k->first_boom = false;
        k->rank = 0;
        k->intro_at = k->reset_at = k->reveal_at = k->explode_at = k->wave_at = -1;
        double d = hypot(i % cols - mc, i / cols - mr);
        spring_to(&k->hover, 0);
        if (intro)
        {
            /* Tiles bloom in from the middle. */
            spring_set(&k->open, 0, 0);
            spring_set(&k->boom, 0, 0);
            spring_set(&k->galpha, 0, 0);
            spring_set(&k->gscale, 1, 1);
            spring_set(&k->kx, 0, 0);
            spring_set(&k->ky, 0, 0);
            spring_set(&k->pop, 0, 0);
            k->intro_at = g->now + 0.12 + d * spread;
        }
        else if (k->open.value > 1e-3 || k->galpha.value > 1e-3 || k->boom.value > 1e-3)
            k->reset_at = g->now + d * spread * 0.8;
        activate(g, i);
    }
}

/* Update */

static void update_bits(game_page *g, bit *pool, int count, double dt)
{
    for (int i = 0; i < count; ++i)
    {
        bit *b = &pool[i];
        if (!b->live || g->now < b->start)
            continue;
        double q = (g->now - b->start) / b->life;
        if (q >= 1)
        {
            b->live = false;
            EX_OK(gr_layer_set_hidden(b->layer, true));
            continue;
        }
        b->v.y += b->gravity * dt;
        double drag = exp(-b->drag * dt);
        b->v.x *= drag;
        b->v.y *= drag;
        b->p.x += b->v.x * dt;
        b->p.y += b->v.y * dt;
        b->angle += b->spin * dt;
        double s = 1 - 0.4 * q;
        /* A flip around the long axis makes confetti flutter. */
        double flip = cos(b->angle * 1.7);
        gr_affine t = {cos(b->angle) * s, sin(b->angle) * s, -sin(b->angle) * s * flip, cos(b->angle) * s * flip, 0, 0};
        EX_OK(gr_layer_set_hidden(b->layer, false));
        EX_OK(gr_layer_set_position(b->layer, b->p));
        EX_OK(gr_layer_set_transform(b->layer, t));
        EX_OK(gr_layer_set_opacity(b->layer, (float)(1 - q * q * q)));
    }
}

static void update_effects(game_page *g, double dt)
{
    for (int i = 0; i < RINGS; ++i)
    {
        ring *r = &g->rings[i];
        if (!r->live)
            continue;
        double p = (g->now - r->start) / 0.65;
        if (p >= 1)
        {
            r->live = false;
            EX_OK(gr_layer_set_hidden(r->layer, true));
            continue;
        }
        EX_OK(gr_layer_set_transform(r->layer, scaling(0.05 + ease_out(p) * r->size)));
        EX_OK(gr_layer_set_opacity(r->layer, (float)((1 - p) * (1 - p))));
    }
    update_bits(g, g->bits, BITS, dt);
    update_bits(g, g->flakes, FLAKES, dt);
    g->shake *= exp(-dt * 7);
    if (g->shake < 0.05)
        g->shake = 0;
    EX_OK(gr_layer_set_transform(
        g->panel, translation(EX_POINT(g->shake * sin(g->now * 71), g->shake * 0.7 * cos(g->now * 57)))));
}

static void show_card(game_page *g)
{
    bool won = g->play == PLAY_WON;
    int seconds = (int)(g->ended - g->started);
    EX_OK(gr_layer_set_hidden(g->card_flag, !won));
    EX_OK(gr_layer_set_hidden(g->card_mine, won));
    label_set(g->card_title, won ? "Field cleared" : "Boom.");
    label_set(g->card_detail, "%02d:%02d  ·  %d × %d  ·  %d mines", seconds / 60 % 100, seconds % 60, g->cols, g->rows,
              g->count);
    spring_to(&g->card_t, 1);
}

static void update_hud(game_page *g, int steps)
{
    double elapsed = g->play == PLAY_RUNNING ? g->now - g->started : g->play == PLAY_READY ? 0 : g->ended - g->started;
    int seconds = (int)fmax(0, elapsed);
    label_set(g->time_text, "%02d:%02d", seconds / 60 % 100, seconds % 60);
    label_set(g->flag_text, "%d", g->count - g->flags);
    label_color(g->flag_text, g->flags > g->count ? hex(C_BOOM) : hex(C_INK));
    spring_run(&g->flag_pop, CHIP, steps);
    EX_OK(gr_layer_set_transform(g->flag_icon, rotation(-0.06 * g->flag_pop.value)));
    EX_OK(gr_layer_set_transform(g->flag_text->layer, scaling(1 + 0.05 * g->flag_pop.value)));

    if (g->card_at >= 0 && g->now >= g->card_at)
    {
        g->card_at = -1;
        show_card(g);
    }
    spring_run(&g->card_t, CARD, steps);
    double t = g->card_t.value;
    bool hidden = t < 0.002 && g->card_t.target == 0;
    EX_OK(gr_layer_set_hidden(g->veil, hidden));
    EX_OK(gr_layer_set_hidden(g->card, hidden));
    EX_OK(gr_layer_set_opacity(g->veil, (float)clampd(0.35 * t, 0, 1)));
    /* The board and header blur away as the card comes up. */
    EX_OK(gr_layer_set_blur(g->backdrop, hidden ? 0 : 7 * clampd(t, 0, 1.2)));
    EX_OK(gr_layer_set_opacity(g->card, (float)clampd(t * 1.5, 0, 1)));
    gr_affine a = scaling(g->card_scale * (0.9 + 0.1 * t));
    a.ty = (1 - t) * 22;
    EX_OK(gr_layer_set_transform(g->card, a));

    button_update(&g->back, steps);
    button_update(&g->restart, steps);
    button_update(&g->again, steps);
    button_update(&g->sizes, steps);
}

void game_update(mines *m, int steps)
{
    game_page *g = &m->game;
    g->now = m->now;
    int n = g->cols * g->rows;

    spring_run(&g->depth, g->depth.target > 0.5 ? DEPTH_IN : DEPTH_OUT, steps);
    spring_run(&g->cx, FOLLOW, steps);
    spring_run(&g->cy, FOLLOW, steps);
    if (n && fabs(g->depth.value) > 1e-4)
    {
        double r = press_radius(g) + g->pitch;
        int c0 = (int)clampd(floor((g->cx.value - r) / g->pitch), 0, g->cols - 1);
        int c1 = (int)clampd(ceil((g->cx.value + r) / g->pitch), 0, g->cols - 1);
        int r0 = (int)clampd(floor((g->cy.value - r) / g->pitch), 0, g->rows - 1);
        int r1 = (int)clampd(ceil((g->cy.value + r) / g->pitch), 0, g->rows - 1);
        for (int y = r0; y <= r1; ++y)
            for (int x = c0; x <= c1; ++x)
                activate(g, y * g->cols + x);
    }

    for (int a = 0; a < g->active_count;)
    {
        int i = g->active[a];
        cell *k = &g->cells[i];
        if (i >= n)
        {
            k->active = false;
            g->active[a] = g->active[--g->active_count];
            continue;
        }
        if (due(&k->intro_at, g->now))
            spring_to(&k->pop, 1);
        if (due(&k->reset_at, g->now))
        {
            reset_visual(k);
            spring_kick(&k->pop, -2);
        }
        if (due(&k->reveal_at, g->now))
            reveal_visual(g, k);
        if (due(&k->explode_at, g->now))
            explode(g, i);
        if (due(&k->wave_at, g->now))
            wave_visual(g, k);
        spring_run(&k->hover, HOVER, steps);
        spring_run(&k->pop, POP, steps);
        spring_run(&k->open, OPENING, steps);
        spring_run(&k->boom, BOOM, steps);
        spring_run(&k->gscale, GLYPH, steps);
        spring_run(&k->galpha, FADE, steps);
        spring_run(&k->kx, KICK, steps);
        spring_run(&k->ky, KICK, steps);
        apply(g, i);
        bool pending =
            k->intro_at >= 0 || k->reset_at >= 0 || k->reveal_at >= 0 || k->explode_at >= 0 || k->wave_at >= 0;
        bool moving = k->hover.active || k->pop.active || k->open.active || k->boom.active || k->gscale.active ||
                      k->galpha.active || k->kx.active || k->ky.active;
        if (!pending && !moving && press_factor(g, cell_x(g, i), cell_y(g, i)) == 0)
        {
            k->active = false;
            g->active[a] = g->active[--g->active_count];
            continue;
        }
        ++a;
    }

    update_effects(g, steps * STEP);
    update_hud(g, steps);
}

/* Input */

static int cell_at(const game_page *g, gr_point p)
{
    if (!g->cols)
        return -1;
    double x = p.x - g->board_rect.origin.x, y = p.y - g->board_rect.origin.y;
    if (x < 0 || y < 0 || x >= g->board_rect.size.width || y >= g->board_rect.size.height)
        return -1;
    int c = (int)clampd(x / g->pitch, 0, g->cols - 1), r = (int)clampd(y / g->pitch, 0, g->rows - 1);
    return r * g->cols + c;
}

static void set_hover(game_page *g, int i)
{
    if (i == g->hover)
        return;
    if (g->hover >= 0)
    {
        spring_to(&g->cells[g->hover].hover, 0);
        activate(g, g->hover);
    }
    g->hover = i;
    if (i >= 0)
    {
        spring_to(&g->cells[i].hover, 1);
        activate(g, i);
    }
}

static void restart(mines *m)
{
    game_page *g = &m->game;
    game_new(m, g->cols, g->rows, g->count, false);
}

void game_reset_pointer(mines *m)
{
    game_page *g = &m->game;
    g->pressing = false;
    spring_to(&g->depth, 0);
    set_hover(g, -1);
    button *buttons[4] = {&g->back, &g->restart, &g->again, &g->sizes};
    for (int i = 0; i < 4; ++i)
    {
        buttons[i]->held = false;
        button_hover(buttons[i], false);
        spring_to(&buttons[i]->press, 0);
    }
}

static void press_button(mines *m, int which)
{
    switch (which)
    {
        case 0:
        case 3:
            go_setup(m);
            break;
        default:
            restart(m);
            break;
    }
}

static bool pointer(mines *m, const gr_event *e)
{
    game_page *g = &m->game;
    const gr_pointer_event *p = &e->value.pointer;
    bool gone = e->type == GR_EVENT_POINTER_LEAVE || e->type == GR_EVENT_POINTER_CANCEL;
    bool card = g->card_t.target > 0.5;
    bool playing = g->play == PLAY_READY || g->play == PLAY_RUNNING;
    button *buttons[4] = {&g->back, &g->restart, &g->again, &g->sizes};
    int hit = -1;
    for (int b = 0; b < (card ? 4 : 2) && !gone; ++b)
        if (button_hit(buttons[b], p->position))
            hit = b;
    for (int b = 0; b < 4; ++b)
        button_hover(buttons[b], b == hit);
    int i = !gone && !card && playing && hit < 0 ? cell_at(g, p->position) : -1;
    set_hover(g, i >= 0 && g->cells[i].state != OPEN ? i : -1);
    bool clickable = i >= 0 && g->cells[i].state != OPEN;
    set_cursor(m, hit >= 0 || clickable ? GR_CURSOR_POINTER : GR_CURSOR_DEFAULT);
    gr_point local = {clampd(p->position.x - g->board_rect.origin.x, 0, g->board_rect.size.width),
                      clampd(p->position.y - g->board_rect.origin.y, 0, g->board_rect.size.height)};

    if (e->type == GR_EVENT_POINTER_DOWN)
    {
        bool flagging = p->button == GR_BUTTON_SECONDARY ||
                        (p->button == GR_BUTTON_PRIMARY && (e->modifiers & GR_MODIFIER_CONTROL));
        if (hit >= 0 && p->button == GR_BUTTON_PRIMARY)
        {
            buttons[hit]->held = true;
            spring_to(&buttons[hit]->press, 1);
        }
        else if (clickable && flagging)
            flag(g, i);
        /* Right click (Control-click on a Mac trackpad) or middle click on an opened number opens its neighbors. */
        else if (i >= 0 && !clickable && (flagging || p->button == GR_BUTTON_MIDDLE))
            chord(g, i);
        else if (clickable && p->button == GR_BUTTON_PRIMARY)
        {
            g->pressing = true;
            if (fabs(g->depth.value) < 0.05)
            {
                spring_set(&g->cx, local.x, local.x);
                spring_set(&g->cy, local.y, local.y);
            }
            else
            {
                spring_to(&g->cx, local.x);
                spring_to(&g->cy, local.y);
            }
            spring_to(&g->depth, 1);
        }
    }
    if (e->type == GR_EVENT_POINTER_MOVE && g->pressing)
    {
        spring_to(&g->cx, local.x);
        spring_to(&g->cy, local.y);
    }
    if (e->type == GR_EVENT_POINTER_UP || gone)
    {
        for (int b = 0; b < 4; ++b)
        {
            if (!buttons[b]->held)
                continue;
            buttons[b]->held = false;
            spring_to(&buttons[b]->press, 0);
            if (e->type == GR_EVENT_POINTER_UP && b == hit)
                press_button(m, b);
        }
        if (g->pressing && (gone || p->button == GR_BUTTON_PRIMARY))
        {
            g->pressing = false;
            spring_to(&g->depth, 0);
            if (e->type == GR_EVENT_POINTER_UP && i >= 0)
                reveal(g, i);
        }
    }
    return true;
}

static bool key(mines *m, const gr_event *e)
{
    game_page *g = &m->game;
    const gr_key_event *k = &e->value.key;
    if (e->modifiers & (GR_MODIFIER_ALT | GR_MODIFIER_SUPER | GR_MODIFIER_CONTROL))
        return false;
    if (e->type != GR_EVENT_KEY_DOWN || k->repeat)
        return true;
    if (k->key == GR_KEY_ESCAPE)
        go_setup(m);
    else if (k->key == GR_KEY_ENTER && g->card_t.target > 0.5)
        restart(m);
    else if (k->key == GR_KEY_CHARACTER && k->character == 'r')
        restart(m);
    else if (k->key == GR_KEY_CHARACTER && (k->character == ' ' || k->character == 'f') && g->hover >= 0)
        flag(g, g->hover);
    return true;
}

bool game_input(mines *m, const gr_event *e)
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
        case GR_EVENT_KEY_DOWN:
        case GR_EVENT_KEY_UP:
            return key(m, e);
        default:
            return false;
    }
}
