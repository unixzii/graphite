/* Mines: pick a field size, then sweep it. The size picker and the board are two pages that cross-fade; the window
 * size drives both layouts. */
#include "mines.h"

static const spring_params PAGE = {0.55, 0.9};

int max_mines(int cols, int rows)
{
    int n = cols * rows;
    return n > 9 ? n - 9 : n - 1;
}

int mine_count(int cols, int rows)
{
    int n = cols * rows;
    if (cols == 9 && rows == 9)
        return 10;
    if (cols == 16 && rows == 16)
        return 40;
    if ((cols == 30 && rows == 16) || (cols == 16 && rows == 30))
        return 99;
    double density = 0.12 + 0.09 * clampd((n - 81) / 399.0, 0, 1);
    int max = max_mines(cols, rows);
    return (int)clampd((double)lround(n * density), max > 0 ? 1 : 0, max);
}

void set_cursor(mines *m, gr_cursor cursor)
{
    if (cursor == m->cursor || !m->ex->host)
        return;
    m->cursor = cursor;
    gr_scene_host_set_cursor(m->ex->host, cursor);
}

void go_game(mines *m, int cols, int rows, int count)
{
    setup_reset_pointer(m);
    m->page = 1;
    spring_to(&m->page_t, 1);
    game_new(m, cols, rows, count, true);
    set_cursor(m, GR_CURSOR_DEFAULT);
}

void go_setup(mines *m)
{
    game_reset_pointer(m);
    m->page = 0;
    spring_to(&m->page_t, 0);
    set_cursor(m, GR_CURSOR_DEFAULT);
}

/* The size picker lifts away while the board settles in from below, and back. */
static void apply_pages(mines *m)
{
    double t = m->page_t.value;
    double out = clampd(1 - t, 0, 1), in = clampd(t, 0, 1);
    EX_OK(gr_layer_set_hidden(m->setup.page, out < 0.002));
    EX_OK(gr_layer_set_opacity(m->setup.page, (float)out));
    gr_affine a = scaling(1 - 0.04 * t);
    a.ty = -30 * t;
    EX_OK(gr_layer_set_transform(m->setup.page, a));
    EX_OK(gr_layer_set_hidden(m->game.page, in < 0.002));
    EX_OK(gr_layer_set_opacity(m->game.page, (float)in));
    gr_affine b = scaling(0.96 + 0.04 * t);
    b.ty = 30 * (1 - t);
    EX_OK(gr_layer_set_transform(m->game.page, b));
}

static void *build(example *ex)
{
    mines *m = calloc(1, sizeof(*m));
    if (!m)
        abort();
    m->ex = ex;
    m->view = ex->desc->size;
    background(gr_scene_get_root(ex->scene), hex(C_PAGE));
    game_build(m);
    setup_build(m);
    apply_pages(m);
    return m;
}

static void update(void *state, double time)
{
    mines *m = state;
    double dt = clampd(time - m->now, 0, 0.1);
    m->now = time;
    m->carry += dt;
    int steps = (int)(m->carry / STEP);
    m->carry -= steps * STEP;
    spring_run(&m->page_t, PAGE, steps);
    apply_pages(m);
    setup_update(m, steps);
    if (m->game.cols)
        game_update(m, steps);
}

static void viewport(void *state, gr_size size)
{
    mines *m = state;
    if (size.width == m->view.width && size.height == m->view.height)
        return;
    m->view = size;
    EX_OK(gr_layer_set_bounds(gr_scene_get_root(m->ex->scene), EX_RECT(0, 0, size.width, size.height)));
    setup_layout(m);
    game_layout(m);
}

static void focus(void *state, bool focused)
{
    mines *m = state;
    m->setup.window_focused = focused;
}

static bool input(void *state, const gr_event *e)
{
    mines *m = state;
    return m->page == 0 ? setup_input(m, e) : game_input(m, e);
}

static void free_state(void *state)
{
    mines *m = state;
    free(m->game.cells);
    free(m->game.active);
    free(m->game.queue);
    free(m);
}

int main(int argc, char **argv)
{
    static const example_desc desc = {"mines", {920, 720}, build, update, free_state, focus, input, viewport, NULL};
    return example_main(argc, argv, &desc);
}
