#include "internal.h"

struct gr_path
{
    atomic_size_t refs;
    uint64_t id;
    gr_path_element_list elements;
};

struct gr_path_builder
{
    size_t refs, count, capacity;
    gr_path_element *data;
};

static bool valid_elements(gr_path_element_list elements)
{
    if ((elements.count && !elements.data) || elements.count > SIZE_MAX / sizeof(*elements.data))
        return false;
    for (size_t i = 0; i < elements.count; ++i)
    {
        const gr_path_element *e = &elements.data[i];
        unsigned n;
        switch (e->verb)
        {
            case GR_PATH_MOVE:
            case GR_PATH_LINE:
                n = 1;
                break;
            case GR_PATH_QUAD:
                n = 2;
                break;
            case GR_PATH_CUBIC:
                n = 3;
                break;
            case GR_PATH_CLOSE:
                n = 0;
                break;
            default:
                return false;
        }
        for (unsigned j = 0; j < n; ++j)
            if (!gr_valid_point(e->p[j]))
                return false;
    }
    return true;
}

gr_path *gr_path_new(gr_path_element_list elements)
{
    if (!valid_elements(elements))
        return NULL;
    gr_path *p = gr_alloc(1, sizeof(*p));
    atomic_init(&p->refs, 1);
    p->id = gr_next_id();
    p->elements =
        (gr_path_element_list){gr_copy(elements.data, elements.count, sizeof(*elements.data)), elements.count};
    return p;
}

gr_path *gr_path_ref(gr_path *p)
{
    if (p)
        gr_shared_ref(&p->refs);
    return p;
}

void gr_path_unref(gr_path *p)
{
    if (!p || !gr_shared_unref(&p->refs))
        return;
    free((void *)p->elements.data);
    free(p);
}

gr_path_element_list gr_path_get_elements(const gr_path *p)
{
    return p->elements;
}

uint64_t gr_path_get_id(const gr_path *p)
{
    return p->id;
}

gr_path_builder *gr_path_builder_new(void)
{
    gr_path_builder *b = gr_alloc(1, sizeof(*b));
    *b = (gr_path_builder){.refs = 1};
    return b;
}

gr_path_builder *gr_path_builder_ref(gr_path_builder *b)
{
    if (b)
        gr_local_ref(&b->refs);
    return b;
}

void gr_path_builder_unref(gr_path_builder *b)
{
    if (!b || !gr_local_unref(&b->refs))
        return;
    free(b->data);
    free(b);
}

gr_result gr_path_builder_append(gr_path_builder *b, gr_path_element_list elements)
{
    if (!valid_elements(elements) || elements.count > SIZE_MAX - b->count)
        return GR_ERROR_ARGUMENT;
    void *data = b->data;
    gr_reserve(&data, &b->capacity, b->count + elements.count, sizeof(*b->data));
    b->data = data;
    if (elements.count)
        memcpy(b->data + b->count, elements.data, elements.count * sizeof(*elements.data));
    b->count += elements.count;
    return GR_OK;
}

gr_path *gr_path_builder_finish(gr_path_builder *b)
{
    gr_path *p = gr_alloc(1, sizeof(*p));
    atomic_init(&p->refs, 1);
    p->id = gr_next_id();
    p->elements = (gr_path_element_list){b->data, b->count};
    b->data = NULL;
    b->count = b->capacity = 0;
    return p;
}

/* Reserves room for up to n elements built here, which need no validation; returns where they go. */
static gr_path_element *room(gr_path_builder *b, size_t n)
{
    void *data = b->data;
    gr_reserve(&data, &b->capacity, b->count + n, sizeof(*b->data));
    b->data = data;
    return b->data + b->count;
}

static gr_path_element *point_to(gr_path_element *e, gr_path_verb verb, double x, double y)
{
    *e = (gr_path_element){verb, {{x, y}}};
    return e + 1;
}

static gr_path_element *cubic_to(gr_path_element *e, double x1, double y1, double x2, double y2, double x, double y)
{
    *e = (gr_path_element){GR_PATH_CUBIC, {{x1, y1}, {x2, y2}, {x, y}}};
    return e + 1;
}

static gr_result close_path(gr_path_builder *b, gr_path_element *e)
{
    e = point_to(e, GR_PATH_CLOSE, 0, 0);
    b->count = (size_t)(e - b->data);
    return GR_OK;
}

gr_result gr_path_builder_add_rect(gr_path_builder *b, gr_rect r)
{
    return gr_path_builder_add_rounded_rect(b, r, 0, 0);
}

gr_result gr_path_builder_add_ellipse(gr_path_builder *b, gr_rect r)
{
    if (!gr_valid_rect(r))
        return GR_ERROR_ARGUMENT;
    const double k = 0.5522847498307936;
    double rx = r.size.width / 2, ry = r.size.height / 2;
    double cx = r.origin.x + rx, cy = r.origin.y + ry, ox = rx * k, oy = ry * k;
    gr_path_element *e = room(b, 6);
    e = point_to(e, GR_PATH_MOVE, cx + rx, cy);
    e = cubic_to(e, cx + rx, cy + oy, cx + ox, cy + ry, cx, cy + ry);
    e = cubic_to(e, cx - ox, cy + ry, cx - rx, cy + oy, cx - rx, cy);
    e = cubic_to(e, cx - rx, cy - oy, cx - ox, cy - ry, cx, cy - ry);
    e = cubic_to(e, cx + ox, cy - ry, cx + rx, cy - oy, cx + rx, cy);
    return close_path(b, e);
}

gr_result gr_path_builder_add_rounded_rect(gr_path_builder *b, gr_rect r, double radius, gr_corners corners)
{
    if (!gr_valid_rect(r) || !isfinite(radius) || radius < 0 || (corners & ~GR_CORNERS_ALL))
        return GR_ERROR_ARGUMENT;
    radius = fmin(radius, fmin(r.size.width, r.size.height) / 2);
    double x = r.origin.x, y = r.origin.y, right = x + r.size.width, bottom = y + r.size.height;
    double tl = corners & GR_CORNER_TOP_LEFT ? radius : 0;
    double tr = corners & GR_CORNER_TOP_RIGHT ? radius : 0;
    double bl = corners & GR_CORNER_BOTTOM_LEFT ? radius : 0;
    double br = corners & GR_CORNER_BOTTOM_RIGHT ? radius : 0;
    double k = radius * (1 - 0.5522847498307936);
    bool round = radius > 0 && corners; /* A plain rectangle closes from its last corner. */
    gr_path_element *e = room(b, 10);
    e = point_to(e, GR_PATH_MOVE, x + tl, y);
    e = point_to(e, GR_PATH_LINE, right - tr, y);
    if (tr)
        e = cubic_to(e, right - k, y, right, y + k, right, y + tr);
    e = point_to(e, GR_PATH_LINE, right, bottom - br);
    if (br)
        e = cubic_to(e, right, bottom - k, right - k, bottom, right - br, bottom);
    e = point_to(e, GR_PATH_LINE, x + bl, bottom);
    if (bl)
        e = cubic_to(e, x + k, bottom, x, bottom - k, x, bottom - bl);
    if (round)
        e = point_to(e, GR_PATH_LINE, x, y + tl);
    if (tl)
        e = cubic_to(e, x, y + k, x + k, y, x + tl, y);
    return close_path(b, e);
}
