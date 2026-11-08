#include "containers.h"
#include "internal.h"

#include <emscripten/emscripten.h>
#include <stdlib.h>

/* The drawing a drawn layer last sent, so commits that keep version and bounds do not redraw it. */
typedef struct
{
    gr_layer *layer;
    uint64_t version;
    gr_rect bounds;
    UT_hash_handle hh;
} drawn_key;

struct gr_web_backend
{
    gr_scene_link *scene;
    int32_t host;
    bool scheduled;
    double *words;
    size_t count, capacity;
    drawn_key *drawn;
    /* Display lists sent in this apply, kept until the runtime has replayed them: their ops reference text layouts,
     * which the runtime reads during replay. */
    gr_display_list **lists;
    size_t list_count, list_capacity;
};

static void push(gr_web_backend *b, double word)
{
    if (b->count == b->capacity)
    {
        size_t capacity = b->capacity ? b->capacity * 2 : 1024;
        double *grown = realloc(b->words, capacity * sizeof(*grown));
        if (!grown)
            abort();
        b->words = grown;
        b->capacity = capacity;
    }
    b->words[b->count++] = word;
}

static void push_id(gr_web_backend *b, const gr_layer *layer)
{
    push(b, (double)(uintptr_t)layer);
}

static void push_color(gr_web_backend *b, gr_color c)
{
    push(b, c.r);
    push(b, c.g);
    push(b, c.b);
    push(b, c.a);
}

static void push_rect(gr_web_backend *b, gr_rect r)
{
    push(b, r.origin.x);
    push(b, r.origin.y);
    push(b, r.size.width);
    push(b, r.size.height);
}

static void push_path_at(gr_web_backend *b, const gr_path *path, gr_point offset)
{
    gr_path_element_list elements = gr_path_get_elements(path);
    push(b, (double)elements.count);
    for (size_t i = 0; i < elements.count; ++i)
    {
        const gr_path_element *e = &elements.data[i];
        size_t points = e->verb == GR_PATH_CUBIC ? 3 : e->verb == GR_PATH_QUAD ? 2 : e->verb == GR_PATH_CLOSE ? 0 : 1;
        push(b, e->verb);
        for (size_t j = 0; j < points; ++j)
        {
            push(b, e->p[j].x + offset.x);
            push(b, e->p[j].y + offset.y);
        }
    }
}

static void push_path(gr_web_backend *b, const gr_path *path)
{
    push_path_at(b, path, (gr_point){0, 0});
}

static void push_stops(gr_web_backend *b, gr_gradient_stop_list stops)
{
    push(b, (double)stops.count);
    for (size_t i = 0; i < stops.count; ++i)
    {
        push(b, stops.data[i].offset);
        push_color(b, stops.data[i].color);
    }
}

static void push_paint(gr_web_backend *b, const gr_paint *paint)
{
    if (!paint)
    {
        push(b, 0);
        return;
    }
    gr_paint_desc d = gr_paint_get_desc(paint);
    switch (d.kind)
    {
        case GR_PAINT_SOLID:
            push(b, 1);
            push_color(b, d.value.solid);
            break;
        case GR_PAINT_LINEAR:
            push(b, 2);
            push(b, d.value.linear.start.x);
            push(b, d.value.linear.start.y);
            push(b, d.value.linear.end.x);
            push(b, d.value.linear.end.y);
            push_stops(b, d.value.linear.stops);
            break;
        case GR_PAINT_RADIAL:
            push(b, 3);
            push(b, d.value.radial.center.x);
            push(b, d.value.radial.center.y);
            push(b, d.value.radial.radius);
            push_stops(b, d.value.radial.stops);
            break;
    }
}

static void push_stroke(gr_web_backend *b, const gr_stroke *stroke)
{
    push(b, stroke != NULL);
    if (!stroke)
        return;
    gr_stroke_style s = gr_stroke_get_style(stroke);
    push(b, s.width);
    push(b, s.cap);
    push(b, s.join);
    push(b, s.miter_limit);
    push(b, (double)s.dash.count);
    for (size_t i = 0; i < s.dash.count; ++i)
        push(b, s.dash.data[i]);
    push(b, s.dash_phase);
    push_paint(b, gr_stroke_get_paint(stroke));
}

static void push_image(gr_web_backend *b, const gr_image *image)
{
    push(b, (double)gr_image_get_id(image));
    push(b, gr_image_get_width(image));
    push(b, gr_image_get_height(image));
    push(b, (double)(uintptr_t)gr_image_get_pixels(image).data);
}

/* Display list ops, each its gr_draw_op_kind and operands, then GR_WEB_END. Text layouts from other text systems
 * are skipped, as text.h asks. */
static void push_ops(gr_web_backend *b, const gr_display_list *list)
{
    gr_draw_op_list ops = gr_display_list_get_ops(list);
    for (size_t i = 0; i < ops.count; ++i)
    {
        const gr_draw_op *op = &ops.data[i];
        switch (op->kind)
        {
            case GR_DRAW_SAVE:
            case GR_DRAW_RESTORE:
                push(b, op->kind);
                break;
            case GR_DRAW_TRANSFORM:
            {
                const gr_affine *m = &op->value.transform;
                push(b, op->kind);
                push(b, m->a);
                push(b, m->b);
                push(b, m->c);
                push(b, m->d);
                push(b, m->tx);
                push(b, m->ty);
                break;
            }
            case GR_DRAW_ALPHA:
                push(b, op->kind);
                push(b, op->value.alpha);
                break;
            case GR_DRAW_CLIP_RECT:
                push(b, op->kind);
                push_rect(b, op->value.clip_rect);
                break;
            case GR_DRAW_CLIP_PATH:
                push(b, op->kind);
                push(b, op->value.clip_path.rule);
                push_path(b, op->value.clip_path.path);
                break;
            case GR_DRAW_FILL_PATH:
                push(b, op->kind);
                push(b, op->value.fill_path.rule);
                push_path(b, op->value.fill_path.path);
                push_paint(b, op->value.fill_path.paint);
                break;
            case GR_DRAW_STROKE_PATH:
                push(b, op->kind);
                push_path(b, op->value.stroke_path.path);
                push_stroke(b, op->value.stroke_path.stroke);
                break;
            case GR_DRAW_IMAGE:
                push(b, op->kind);
                push_image(b, op->value.image.image);
                push_rect(b, op->value.image.destination);
                break;
            case GR_DRAW_TEXT_LAYOUT:
            case GR_DRAW_TEXT_GLYPHS:
            {
                /* Layouts from other text systems are skipped. */
                gr_text_layout *layout =
                    op->kind == GR_DRAW_TEXT_LAYOUT ? op->value.text_layout.layout : op->value.text_glyphs.layout;
                int32_t id = gr_web_layout_id(layout);
                if (!id)
                    break;
                push(b, op->kind);
                push(b, id);
                if (op->kind == GR_DRAW_TEXT_LAYOUT)
                {
                    push(b, op->value.text_layout.origin.x);
                    push(b, op->value.text_layout.origin.y);
                    push_color(b, op->value.text_layout.color);
                }
                else
                {
                    push(b, op->value.text_glyphs.origin.x);
                    push(b, op->value.text_glyphs.origin.y);
                    push_color(b, op->value.text_glyphs.color);
                    push(b, (double)op->value.text_glyphs.line);
                    push(b, (double)op->value.text_glyphs.glyphs.start);
                    push(b, (double)op->value.text_glyphs.glyphs.end);
                }
                break;
            }
        }
    }
    push(b, GR_WEB_END);
}

static void forget_drawing(gr_web_backend *b, gr_layer *layer)
{
    drawn_key *key = NULL;
    HASH_FIND_PTR(b->drawn, &layer, key);
    if (key)
    {
        HASH_DEL(b->drawn, key);
        free(key);
    }
}

/* Sends a drawn layer's display list when its version or drawing bounds changed since the last one sent. */
static void push_drawing(gr_web_backend *b, const gr_commit_batch *batch, const gr_layer_update *u)
{
    gr_draw_info info = gr_resolved_get_draw_info(&u->resolved, batch->backing_scale);
    uint64_t version = u->resolved.content.value.display_version;
    drawn_key *key = NULL;
    HASH_FIND_PTR(b->drawn, &u->layer, key);
    if (key && key->version == version && memcmp(&key->bounds, &info.bounds, sizeof(info.bounds)) == 0)
        return;
    gr_display_list *list = NULL;
    if (gr_drawer_provider_draw(batch->drawers, u->layer, version, &info, &list) != GR_OK)
        return;
    if (!key)
    {
        key = gr_alloc(1, sizeof(*key));
        key->layer = u->layer;
        HASH_ADD_PTR(b->drawn, layer, key);
    }
    key->version = version;
    key->bounds = info.bounds;
    push(b, GR_WEB_CONTENT);
    push_id(b, u->layer);
    push(b, GR_CONTENT_DRAWN);
    push_ops(b, list);
    if (b->list_count == b->list_capacity)
    {
        size_t capacity = b->list_capacity ? b->list_capacity * 2 : 16;
        gr_display_list **grown = realloc(b->lists, capacity * sizeof(*grown));
        if (!grown)
            abort();
        b->lists = grown;
        b->list_capacity = capacity;
    }
    b->lists[b->list_count++] = list;
}

static void push_content(gr_web_backend *b, const gr_commit_batch *batch, const gr_layer_update *u)
{
    const gr_resolved_content *c = &u->resolved.content;
    if (c->kind == GR_CONTENT_DRAWN)
    {
        push_drawing(b, batch, u);
        return;
    }
    forget_drawing(b, u->layer);
    push(b, GR_WEB_CONTENT);
    push_id(b, u->layer);
    push(b, c->kind);
    switch (c->kind)
    {
        case GR_CONTENT_NONE:
        case GR_CONTENT_DRAWN:
            break;
        case GR_CONTENT_IMAGE:
            push_image(b, c->value.image.image);
            push_rect(b, c->value.image.rect);
            break;
        case GR_CONTENT_SHAPE:
            push(b, c->value.shape.path != NULL);
            if (c->value.shape.path)
                push_path(b, c->value.shape.path);
            push_paint(b, c->value.shape.fill);
            push(b, c->value.shape.fill_rule);
            push_stroke(b, c->value.shape.stroke);
            push(b, c->value.shape.stroke_start);
            push(b, c->value.shape.stroke_end);
            break;
        case GR_CONTENT_GRADIENT:
            push_paint(b, c->value.gradient);
            break;
    }
}

static void push_update(gr_web_backend *b, const gr_commit_batch *batch, const gr_layer_update *u)
{
    const gr_resolved *r = &u->resolved;
    const gr_layer_props *p = &r->props;
    if (u->changed & GR_DIRTY_GEOMETRY)
    {
        push(b, GR_WEB_GEOMETRY);
        push_id(b, u->layer);
        push(b, p->bounds.size.width);
        push(b, p->bounds.size.height);
        push(b, r->transform.a);
        push(b, r->transform.b);
        push(b, r->transform.c);
        push(b, r->transform.d);
        push(b, r->transform.tx);
        push(b, r->transform.ty);
        push(b, -p->bounds.origin.x);
        push(b, -p->bounds.origin.y);
    }
    if (u->changed & GR_DIRTY_APPEARANCE)
    {
        push(b, GR_WEB_APPEARANCE);
        push_id(b, u->layer);
        push(b, p->opacity);
        push(b, p->hidden);
        push(b, p->masks_to_bounds);
        push(b, p->corner_radius);
        push(b, p->masked_corners);
        push(b, p->has_background);
        push_color(b, p->background);
        push(b, p->has_border);
        push(b, p->border.width);
        push_color(b, p->border.color);
        push(b, p->blur_sigma);
    }
    if (u->changed & GR_DIRTY_SHADOW)
    {
        gr_path *outline = gr_resolved_new_shadow_path(r);
        push(b, GR_WEB_SHADOW);
        push_id(b, u->layer);
        push(b, outline != NULL);
        if (outline)
        {
            push_color(b, p->shadow.color);
            push(b, p->shadow.offset.x);
            push(b, p->shadow.offset.y);
            push(b, p->shadow.blur_sigma);
            push_path(b, outline);
            gr_path_unref(outline);
        }
    }
    /* A path mask moves with the bounds origin and a gradient mask stretches over the box. */
    if (u->changed & (GR_DIRTY_GEOMETRY | GR_DIRTY_MASK))
    {
        push(b, GR_WEB_MASK);
        push_id(b, u->layer);
        push(b, p->has_mask ? (p->mask.kind == GR_MASK_PATH ? 1 : 2) : 0);
        if (p->has_mask && p->mask.kind == GR_MASK_PATH)
        {
            push(b, p->mask.value.path.rule == GR_FILL_EVEN_ODD);
            push_path_at(b, p->mask.value.path.path, (gr_point){-p->bounds.origin.x, -p->bounds.origin.y});
        }
        else if (p->has_mask)
            push_paint(b, p->mask.value.gradient);
    }
    /* Image placement and drawing bounds follow geometry; shapes (local space) and gradients (unit space) do not. */
    bool sized = r->content.kind == GR_CONTENT_IMAGE || r->content.kind == GR_CONTENT_DRAWN;
    if (u->changed & GR_DIRTY_CONTENT || (u->changed & GR_DIRTY_GEOMETRY && sized))
        push_content(b, batch, u);
}

EMSCRIPTEN_KEEPALIVE void gr_web_tick(gr_web_backend *backend, gr_web_host *events, double time, double interval)
{
    /* The reference keeps the Scene, and with it this backend, alive through a frame handler that releases the last
     * outside reference. The handler may release the event host, which is not used after it. */
    gr_scene *scene = gr_scene_link_lock(backend->scene);
    if (!scene)
        return;
    if (events)
        gr_web_host_frame(events, time, interval);
    if (backend->scheduled)
    {
        backend->scheduled = false;
        gr_scene_commit(scene);
    }
    gr_scene_unref(scene);
}

static void request_commit(void *p)
{
    gr_web_backend *backend = p;
    if (backend->scheduled)
        return;
    backend->scheduled = true;
    gr_web_js_request_tick(backend->host);
}

static void apply(void *p, const gr_commit_batch *batch)
{
    gr_web_backend *b = p;
    b->count = 0;
    if (batch->root)
    {
        push(b, GR_WEB_ROOT);
        push_id(b, batch->root);
    }
    for (size_t i = 0; i < batch->realized.count; ++i)
    {
        push(b, GR_WEB_REALIZE);
        push_id(b, batch->realized.data[i].layer);
        push(b, batch->realized.data[i].kind);
    }
    for (size_t i = 0; i < batch->children.count; ++i)
    {
        const gr_children_update *update = &batch->children.data[i];
        push(b, GR_WEB_CHILDREN);
        push_id(b, update->parent);
        push(b, (double)update->children.count);
        for (size_t j = 0; j < update->children.count; ++j)
            push_id(b, update->children.data[j]);
    }
    for (size_t i = 0; i < batch->updates.count; ++i)
        push_update(b, batch, &batch->updates.data[i]);
    for (size_t i = 0; i < batch->released.count; ++i)
    {
        forget_drawing(b, batch->released.data[i]);
        push(b, GR_WEB_RELEASE);
        push_id(b, batch->released.data[i]);
    }
    gr_web_js_apply(b->host, b->words, b->count);
    for (size_t i = 0; i < b->list_count; ++i)
        gr_display_list_unref(b->lists[i]);
    b->list_count = 0;
}

static void destroy(void *p)
{
    gr_web_backend *b = p;
    gr_web_js_host_free(b->host);
    gr_scene_link_unref(b->scene);
    drawn_key *key, *tmp;
    HASH_ITER(hh, b->drawn, key, tmp)
    {
        HASH_DEL(b->drawn, key);
        free(key);
    }
    free(b->lists);
    free(b->words);
    free(b);
}

bool gr_web_install(gr_scene *scene, int32_t host)
{
    gr_web_backend *backend = calloc(1, sizeof(*backend));
    gr_scene_link *link = gr_scene_get_link(scene);
    if (!backend || !link)
    {
        free(backend);
        gr_scene_link_unref(link);
        gr_web_js_host_free(host);
        return false;
    }
    backend->scene = link;
    backend->host = host;
    gr_web_js_host_backend(host, backend);
    const gr_backend_vtable vtable = {request_commit, apply, destroy};
    if (gr_scene_set_backend(scene, &vtable, backend) != GR_OK)
    {
        destroy(backend);
        return false;
    }
    return true;
}
