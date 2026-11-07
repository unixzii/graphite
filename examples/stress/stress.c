#include "example.h"

#define GRID_SIDE 80
#define CELL_SIZE 8.0
#define CELL_PITCH 10.0
#define GRID_CENTER 400.0

typedef struct
{
    example *ex;
    gr_layer **cells; /**< Row-major, GRID_SIDE × GRID_SIDE. */
} stress_scene;

static gr_point cell_offset(size_t row, size_t col)
{
    return EX_POINT(((double)col - (GRID_SIDE - 1) * 0.5) * CELL_PITCH,
                    ((double)row - (GRID_SIDE - 1) * 0.5) * CELL_PITCH);
}

static void *build(example *ex)
{
    stress_scene *d = calloc(1, sizeof(*d));
    if (!d)
        abort();
    d->ex = ex;
    d->cells = calloc(GRID_SIDE * GRID_SIDE, sizeof(gr_layer *));
    if (!d->cells)
        abort();
    gr_layer *root = gr_scene_get_root(d->ex->scene);
    background(root, hex(0x0B1020));
    for (size_t row = 0; row < GRID_SIDE; ++row)
        for (size_t col = 0; col < GRID_SIDE; ++col)
        {
            gr_point offset = cell_offset(row, col);
            gr_layer *cell = layer(d->ex, root,
                                   EX_RECT(GRID_CENTER + offset.x - CELL_SIZE * 0.5,
                                           GRID_CENTER + offset.y - CELL_SIZE * 0.5, CELL_SIZE, CELL_SIZE));
            d->cells[row * GRID_SIDE + col] = cell;
        }
    return d;
}

static void update(void *state, double t)
{
    stress_scene *d = state;
    const double period = 3.0, speed = 260.0, duration = 1.1;
    gr_color base = hex(0x244568), crest = hex(0x64E8FF);
    for (size_t row = 0; row < GRID_SIDE; ++row)
        for (size_t col = 0; col < GRID_SIDE; ++col)
        {
            gr_point offset = cell_offset(row, col);
            double distance = hypot(offset.x, offset.y);
            // Retarded time makes each pulse travel from the center to the corners.
            double age = fmod(t, period) - distance / speed;
            double ripple = 0, intensity = 0;
            if (age > 0 && age < duration)
            {
                double phase = age / duration;
                double envelope = sin(EX_PI * phase);
                intensity = envelope * envelope;
                ripple = sin(EX_TAU * phase) * intensity;
            }
            gr_layer *cell = d->cells[row * GRID_SIDE + col];
            gr_affine transform = scaling(1 + 0.4 * ripple);
            if (distance > 0)
            {
                transform.tx = offset.x / distance * 9 * ripple;
                transform.ty = offset.y / distance * 9 * ripple;
            }
            EX_OK(gr_layer_set_transform(cell, transform));
            float blend = (float)intensity;
            background(cell, (gr_color){base.r + (crest.r - base.r) * blend, base.g + (crest.g - base.g) * blend,
                                        base.b + (crest.b - base.b) * blend, 1});
        }
}

static void free_state(void *state)
{
    stress_scene *d = state;
    free(d->cells);
    free(d);
}

int main(int argc, char **argv)
{
    static const example_desc desc = {"stress", {800, 800}, build, update, free_state};
    return example_main(argc, argv, &desc);
}
