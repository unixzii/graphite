#include "internal.h"

struct gr_canvas
{
    size_t refs, count, capacity, depth;
    gr_draw_op *ops;
};

struct gr_display_list
{
    atomic_size_t refs;
    uint64_t id;
    gr_draw_op_list ops;
};

static void release_op(gr_draw_op *op)
{
    switch (op->kind)
    {
        case GR_DRAW_CLIP_PATH:
            gr_path_unref(op->value.clip_path.path);
            break;
        case GR_DRAW_FILL_PATH:
            gr_path_unref(op->value.fill_path.path);
            gr_paint_unref(op->value.fill_path.paint);
            break;
        case GR_DRAW_STROKE_PATH:
            gr_path_unref(op->value.stroke_path.path);
            gr_stroke_unref(op->value.stroke_path.stroke);
            break;
        case GR_DRAW_IMAGE:
            gr_image_unref(op->value.image.image);
            break;
        default:
            break;
    }
}

static void push(gr_canvas *c, gr_draw_op op)
{
    if (c->count == SIZE_MAX)
        abort();
    void *ops = c->ops;
    gr_reserve(&ops, &c->capacity, c->count + 1, sizeof(*c->ops));
    c->ops = ops;
    c->ops[c->count++] = op;
}

gr_canvas *gr_canvas_new(void)
{
    gr_canvas *c = gr_alloc(1, sizeof(*c));
    *c = (gr_canvas){.refs = 1};
    return c;
}

gr_canvas *gr_canvas_ref(gr_canvas *c)
{
    if (c)
        gr_local_ref(&c->refs);
    return c;
}

void gr_canvas_unref(gr_canvas *c)
{
    if (!c || !gr_local_unref(&c->refs))
        return;
    for (size_t i = 0; i < c->count; ++i)
        release_op(&c->ops[i]);
    free(c->ops);
    free(c);
}

void gr_canvas_save(gr_canvas *c)
{
    if (c->depth == SIZE_MAX)
        abort();
    push(c, (gr_draw_op){.kind = GR_DRAW_SAVE});
    ++c->depth;
}

void gr_canvas_restore(gr_canvas *c)
{
    if (!c->depth)
        return;
    push(c, (gr_draw_op){.kind = GR_DRAW_RESTORE});
    --c->depth;
}

gr_result gr_canvas_concat_transform(gr_canvas *c, gr_affine m)
{
    if (!isfinite(m.a) || !isfinite(m.b) || !isfinite(m.c) || !isfinite(m.d) || !isfinite(m.tx) || !isfinite(m.ty))
        return GR_ERROR_ARGUMENT;
    if (m.a != 1 || m.b != 0 || m.c != 0 || m.d != 1 || m.tx != 0 || m.ty != 0)
        push(c, (gr_draw_op){.kind = GR_DRAW_TRANSFORM, .value.transform = m});
    return GR_OK;
}

gr_result gr_canvas_set_alpha(gr_canvas *c, float alpha)
{
    if (!isfinite(alpha))
        return GR_ERROR_ARGUMENT;
    push(c, (gr_draw_op){.kind = GR_DRAW_ALPHA, .value.alpha = fminf(1, fmaxf(0, alpha))});
    return GR_OK;
}

gr_result gr_canvas_clip_rect(gr_canvas *c, gr_rect r)
{
    if (!gr_valid_rect(r))
        return GR_ERROR_ARGUMENT;
    push(c, (gr_draw_op){.kind = GR_DRAW_CLIP_RECT, .value.clip_rect = r});
    return GR_OK;
}

gr_result gr_canvas_clip_path(gr_canvas *c, gr_path *path, gr_fill_rule rule)
{
    if (!path || !gr_valid_rule(rule))
        return GR_ERROR_ARGUMENT;
    gr_draw_op op = {.kind = GR_DRAW_CLIP_PATH};
    op.value.clip_path.path = gr_path_ref(path);
    op.value.clip_path.rule = rule;
    push(c, op);
    return GR_OK;
}

gr_result gr_canvas_fill_path(gr_canvas *c, gr_path *path, gr_fill_rule rule, gr_paint *paint)
{
    if (!path || !paint || !gr_valid_rule(rule))
        return GR_ERROR_ARGUMENT;
    gr_draw_op op = {.kind = GR_DRAW_FILL_PATH};
    op.value.fill_path.path = gr_path_ref(path);
    op.value.fill_path.paint = gr_paint_ref(paint);
    op.value.fill_path.rule = rule;
    push(c, op);
    return GR_OK;
}

gr_result gr_canvas_fill_rect(gr_canvas *c, gr_rect r, gr_paint *paint)
{
    if (!paint || !gr_valid_rect(r))
        return GR_ERROR_ARGUMENT;
    gr_path_builder *b = gr_path_builder_new();
    gr_path_builder_add_rect(b, r);
    gr_path *p = gr_path_builder_finish(b);
    gr_path_builder_unref(b);
    gr_result result = gr_canvas_fill_path(c, p, GR_FILL_NONZERO, paint);
    gr_path_unref(p);
    return result;
}

gr_result gr_canvas_stroke_path(gr_canvas *c, gr_path *path, gr_stroke *stroke)
{
    if (!path || !stroke)
        return GR_ERROR_ARGUMENT;
    gr_draw_op op = {.kind = GR_DRAW_STROKE_PATH};
    op.value.stroke_path.path = gr_path_ref(path);
    op.value.stroke_path.stroke = gr_stroke_ref(stroke);
    push(c, op);
    return GR_OK;
}

gr_result gr_canvas_draw_image(gr_canvas *c, gr_image *image, gr_rect dst)
{
    if (!image || !gr_valid_rect(dst))
        return GR_ERROR_ARGUMENT;
    gr_draw_op op = {.kind = GR_DRAW_IMAGE};
    op.value.image.image = gr_image_ref(image);
    op.value.image.destination = dst;
    push(c, op);
    return GR_OK;
}

gr_display_list *gr_canvas_finish(gr_canvas *c)
{
    while (c->depth)
        gr_canvas_restore(c);
    gr_display_list *list = gr_alloc(1, sizeof(*list));
    atomic_init(&list->refs, 1);
    list->id = gr_next_id();
    list->ops = (gr_draw_op_list){c->ops, c->count};
    c->ops = NULL;
    c->count = c->capacity = 0;
    return list;
}

gr_display_list *gr_display_list_ref(gr_display_list *list)
{
    if (list)
        gr_shared_ref(&list->refs);
    return list;
}

void gr_display_list_unref(gr_display_list *list)
{
    if (!list || !gr_shared_unref(&list->refs))
        return;
    for (size_t i = 0; i < list->ops.count; ++i)
        release_op((gr_draw_op *)&list->ops.data[i]);
    free((void *)list->ops.data);
    free(list);
}

uint64_t gr_display_list_get_id(const gr_display_list *list)
{
    return list->id;
}

bool gr_display_list_is_empty(const gr_display_list *list)
{
    return list->ops.count == 0;
}

gr_draw_op_list gr_display_list_get_ops(const gr_display_list *list)
{
    return list->ops;
}
