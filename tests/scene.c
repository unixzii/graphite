#include "test.h"

#include <graphite/backend.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIMIT 256

typedef struct
{
    gr_layer *id, *parent;
    gr_layer *children[LIMIT];
    size_t count;
    bool alive;
} mirror_node;
typedef struct recorder recorder;

struct recorder
{
    gr_scene *scene;
    size_t requests, applies, destroys, realized, released, update_count;
    gr_layer *root;
    gr_layer_update *updates;
    mirror_node *nodes;
    size_t node_count;
    gr_drawer_provider *provider;
    void (*during_apply)(recorder *, const gr_commit_batch *);
};

static void snapshot_ref(gr_resolved *r)
{
    if (r->props.has_shadow)
        gr_path_ref(r->props.shadow.path);
    switch (r->content.kind)
    {
        case GR_CONTENT_IMAGE:
            gr_image_ref(r->content.value.image.image);
            break;
        case GR_CONTENT_SHAPE:
            gr_path_ref(r->content.value.shape.path);
            gr_paint_ref(r->content.value.shape.fill);
            gr_stroke_ref(r->content.value.shape.stroke);
            break;
        case GR_CONTENT_GRADIENT:
            gr_paint_ref(r->content.value.gradient);
            break;
        default:
            break;
    }
}

static void clear_updates(recorder *r)
{
    for (size_t i = 0; i < r->update_count; ++i)
    {
        gr_resolved *v = &r->updates[i].resolved;
        if (v->props.has_shadow)
            gr_path_unref(v->props.shadow.path);
        switch (v->content.kind)
        {
            case GR_CONTENT_IMAGE:
                gr_image_unref(v->content.value.image.image);
                break;
            case GR_CONTENT_SHAPE:
                gr_path_unref(v->content.value.shape.path);
                gr_paint_unref(v->content.value.shape.fill);
                gr_stroke_unref(v->content.value.shape.stroke);
                break;
            case GR_CONTENT_GRADIENT:
                gr_paint_unref(v->content.value.gradient);
                break;
            default:
                break;
        }
    }
    r->update_count = 0;
}

static mirror_node *node(recorder *r, gr_layer *id)
{
    for (size_t i = 0; i < r->node_count; ++i)
        if (r->nodes[i].alive && r->nodes[i].id == id)
            return &r->nodes[i];
    return NULL;
}

static void request(void *userdata)
{
    recorder *r = userdata;
    ++r->requests;
    CHECK(gr_scene_commit(r->scene) == GR_ERROR_REENTRANT);
    CHECK(gr_scene_set_backing_scale(r->scene, 2) == GR_ERROR_REENTRANT);
}

static void apply(void *userdata, const gr_commit_batch *b)
{
    recorder *r = userdata;
    ++r->applies;
    r->realized = b->realized.count;
    r->released = b->released.count;
    clear_updates(r);
    gr_drawer_provider_unref(r->provider);
    r->provider = gr_drawer_provider_ref(b->drawers);
    if (b->root)
    {
        CHECK(!r->root);
        r->root = b->root;
    }
    for (size_t i = 0; i < b->realized.count; ++i)
    {
        gr_layer *id = b->realized.data[i].layer;
        CHECK(!node(r, id) && r->node_count < LIMIT);
        r->nodes[r->node_count++] = (mirror_node){.id = id, .alive = true};
    }
    /* Mirror the backend's required two-pass reparenting. */
    for (size_t i = 0; i < b->children.count; ++i)
    {
        const gr_children_update *u = &b->children.data[i];
        mirror_node *p = node(r, u->parent);
        CHECK(p);
        for (size_t j = 0; j < p->count; ++j)
        {
            bool found = false;
            for (size_t k = 0; k < u->children.count; ++k)
                found |= p->children[j] == u->children.data[k];
            mirror_node *child = node(r, p->children[j]);
            CHECK(child);
            if (!found && child->parent == p->id)
                child->parent = NULL;
        }
    }
    for (size_t i = 0; i < b->children.count; ++i)
    {
        const gr_children_update *u = &b->children.data[i];
        mirror_node *p = node(r, u->parent);
        CHECK(u->children.count < LIMIT);
        p->count = u->children.count;
        for (size_t j = 0; j < p->count; ++j)
        {
            gr_layer *id = u->children.data[j];
            CHECK(node(r, id));
            p->children[j] = id;
            node(r, id)->parent = p->id;
        }
    }
    CHECK(b->updates.count < LIMIT);
    for (size_t i = 0; i < b->updates.count; ++i)
    {
        const gr_layer_update *u = &b->updates.data[i];
        CHECK(node(r, u->layer) && !(u->changed & GR_DIRTY_CHILDREN));
        if (u->resolved.mask)
            CHECK(node(r, u->resolved.mask));
        r->updates[r->update_count] = *u;
        snapshot_ref(&r->updates[r->update_count++].resolved);
    }
    if (r->during_apply)
        r->during_apply(r, b);
    for (size_t i = 0; i < b->released.count; ++i)
    {
        mirror_node *p = node(r, b->released.data[i]);
        CHECK(p);
        for (size_t j = 0; j < r->node_count; ++j)
            if (r->nodes[j].parent == p->id)
                r->nodes[j].parent = NULL;
        p->alive = false;
    }
}

static void destroy(void *userdata)
{
    recorder *r = userdata;
    ++r->destroys;
    clear_updates(r);
    gr_drawer_provider_unref(r->provider);
    r->provider = NULL;
    free(r->updates);
    free(r->nodes);
    r->updates = NULL;
    r->nodes = NULL;
}

static void init_recorder(recorder *r, gr_scene *scene)
{
    *r = (recorder){.scene = scene};
    r->updates = calloc(LIMIT, sizeof(*r->updates));
    r->nodes = calloc(LIMIT, sizeof(*r->nodes));
    CHECK(r->updates && r->nodes);
}

static gr_scene *new_scene(recorder *r)
{
    init_recorder(r, gr_scene_new((gr_size){400, 300}));
    gr_backend_vtable v = {request, apply, destroy};
    OK(gr_scene_set_backend(r->scene, &v, r));
    return r->scene;
}

static const gr_layer_update *update(recorder *r, gr_layer *l)
{
    for (size_t i = 0; i < r->update_count; ++i)
        if (r->updates[i].layer == l)
            return &r->updates[i];
    return NULL;
}

static void basics(void)
{
    recorder r;
    gr_scene *s = new_scene(&r);
    CHECK(r.requests == 1);
    OK(gr_scene_commit(s));
    CHECK(r.realized == 1 && r.root == gr_scene_get_root(s));
    CHECK(r.updates[0].resolved.transform.a == 1 && r.updates[0].resolved.transform.tx == 0);
    OK(gr_scene_commit(s));
    CHECK(r.applies == 1);
    gr_layer *l = gr_layer_new(s, GR_LAYER_CONTAINER);
    OK(gr_layer_set_opacity(l, 0.5f));
    CHECK(!gr_scene_needs_commit(s));
    OK(gr_layer_add_child(gr_scene_get_root(s), l));
    OK(gr_layer_set_hidden(l, true));
    CHECK(r.requests == 2);
    OK(gr_scene_commit(s));
    CHECK(r.realized == 1 && update(&r, l));
    OK(gr_layer_set_opacity(l, 0.5f));
    OK(gr_layer_set_hidden(l, true));
    CHECK(!gr_scene_needs_commit(s));
    OK(gr_layer_set_opacity(l, 0.3f));
    OK(gr_layer_set_position(l, (gr_point){5, 5}));
    OK(gr_scene_commit(s));
    CHECK(r.update_count == 1 && update(&r, l)->changed == (GR_DIRTY_GEOMETRY | GR_DIRTY_APPEARANCE));
    OK(gr_layer_remove_from_parent(l));
    OK(gr_scene_commit(s));
    CHECK(r.released == 0 && node(&r, l));
    OK(gr_layer_set_position(l, (gr_point){7, 9}));
    CHECK(!gr_scene_needs_commit(s));
    OK(gr_layer_add_child(gr_scene_get_root(s), l));
    OK(gr_scene_commit(s));
    CHECK(r.realized == 0 && update(&r, l)->resolved.props.position.x == 7);
    OK(gr_layer_remove_from_parent(l));
    OK(gr_scene_commit(s));
    size_t before = r.requests;
    gr_layer_unref(l);
    CHECK(r.requests == before + 1 && gr_scene_get_layer_count(s) == 1);
    OK(gr_scene_commit(s));
    CHECK(r.released == 1);
    l = gr_layer_new(s, GR_LAYER_CONTAINER);
    OK(gr_layer_add_child(gr_scene_get_root(s), l));
    OK(gr_layer_remove_from_parent(l));
    gr_layer_unref(l);
    OK(gr_scene_commit(s));
    CHECK(r.realized == 0 && r.released == 0);
    gr_scene_unref(s);
    CHECK(r.destroys == 1);
}

static void tree(void)
{
    recorder r;
    gr_scene *s = new_scene(&r), *other = gr_scene_new((gr_size){1, 1});
    gr_layer *root = gr_scene_get_root(s);
    gr_layer *a = gr_layer_new(s, GR_LAYER_CONTAINER), *b = gr_layer_new(s, GR_LAYER_CONTAINER);
    gr_layer *c = gr_layer_new(s, GR_LAYER_CONTAINER), *mask = gr_layer_new(s, GR_LAYER_SHAPE);
    OK(gr_layer_add_child(root, a));
    OK(gr_layer_add_child(root, b));
    OK(gr_layer_add_child(a, c));
    OK(gr_scene_commit(s));
    OK(gr_layer_add_child(b, c));
    OK(gr_scene_commit(s));
    CHECK(node(&r, c)->parent == b);
    OK(gr_layer_insert_child(root, b, 0));
    CHECK(gr_layer_get_child(root, 0) == b && gr_layer_get_child(root, 1) == a);
    CHECK(gr_layer_add_child(c, b) == GR_ERROR_CYCLE);
    CHECK(gr_layer_add_child(c, c) == GR_ERROR_CYCLE);
    CHECK(gr_layer_add_child(c, root) == GR_ERROR_STATE);
    CHECK(gr_layer_add_child(c, gr_scene_get_root(other)) == GR_ERROR_SCENE);
    CHECK(gr_layer_set_mask(a, c) == GR_ERROR_STATE);
    OK(gr_layer_set_mask(a, mask));
    CHECK(gr_layer_get_parent(mask) == NULL && gr_layer_get_mask(a) == mask);
    CHECK(gr_layer_add_child(root, mask) == GR_ERROR_STATE);
    CHECK(gr_layer_set_mask(mask, c) == GR_ERROR_STATE);
    OK(gr_scene_commit(s));
    CHECK(node(&r, mask));
    OK(gr_layer_remove_from_parent(mask));
    CHECK(!gr_layer_get_mask(a));
    OK(gr_layer_set_mask(a, mask));
    gr_layer_unref(mask); /* Owner keeps mask alive. */
    gr_layer_unref(c);
    gr_layer_unref(a);
    gr_layer_unref(b);
    gr_scene_unref(other);
    gr_scene_unref(s);
    CHECK(r.destroys == 1);
}

typedef struct
{
    gr_scene *s;
    gr_layer *l, *owned;
    size_t draws, destroys;
    gr_drawer_provider *provider;
    uint64_t version;
} draw_state;

static void draw_callback(void *userdata, gr_canvas *canvas, const gr_draw_info *info)
{
    draw_state *d = userdata;
    ++d->draws;
    CHECK(gr_layer_get_kind(d->l) == GR_LAYER_CONTENT);
    CHECK(gr_layer_set_hidden(d->l, true) == GR_ERROR_REENTRANT);
    CHECK(gr_content_layer_clear_drawer(d->l) == GR_ERROR_REENTRANT);
    CHECK(gr_scene_commit(d->s) == GR_ERROR_REENTRANT);
    CHECK(gr_layer_new(d->s, GR_LAYER_CONTAINER) == NULL);
    if (d->provider)
    {
        gr_display_list *nested = NULL;
        CHECK(gr_drawer_provider_draw(d->provider, d->l, d->version, info, &nested) == GR_ERROR_REENTRANT);
        CHECK(!nested);
    }
    OK(gr_canvas_clip_rect(canvas, info->bounds));
}

static void draw_destroy(void *userdata)
{
    draw_state *d = userdata;
    ++d->destroys;
    gr_layer_unref(d->owned);
    d->owned = NULL;
}

static void drawers(void)
{
    recorder r;
    gr_scene *s = new_scene(&r);
    gr_layer *l = gr_layer_new(s, GR_LAYER_CONTENT);
    draw_state d = {.s = s, .l = l};
    gr_drawer drawer = {&d, draw_callback, draw_destroy};
    OK(gr_content_layer_set_drawer(l, &drawer));
    OK(gr_layer_add_child(gr_scene_get_root(s), l));
    OK(gr_scene_commit(s));
    CHECK(d.draws == 0);
    uint64_t version = update(&r, l)->resolved.content.value.display_version;
    d.provider = r.provider;
    d.version = version;
    gr_draw_info info = gr_resolved_get_draw_info(&update(&r, l)->resolved, 1);
    gr_display_list *list = NULL;
    OK(gr_drawer_provider_draw(r.provider, l, version, &info, &list));
    gr_display_list_unref(list);
    OK(gr_drawer_provider_draw(r.provider, l, version, &info, &list));
    gr_display_list_unref(list);
    CHECK(d.draws == 2 && !gr_scene_needs_commit(s));
    OK(gr_scene_set_backing_scale(s, 2));
    OK(gr_scene_commit(s));
    CHECK(update(&r, l)->resolved.content.value.display_version == version);
    OK(gr_content_layer_set_needs_display(l));
    CHECK(gr_drawer_provider_draw(r.provider, l, version, &info, &list) == GR_ERROR_STATE && !list);
    OK(gr_scene_commit(s));
    version = update(&r, l)->resolved.content.value.display_version;
    CHECK(version != d.version);
    d.version = version;
    d.provider = r.provider;
    gr_drawer_provider *provider = gr_drawer_provider_ref(r.provider);
    gr_layer *foreign = gr_layer_new(s, GR_LAYER_CONTENT);
    recorder other_rec;
    gr_scene *other = new_scene(&other_rec);
    OK(gr_scene_commit(other));
    CHECK(gr_drawer_provider_draw(other_rec.provider, foreign, version, &info, &list) == GR_ERROR_SCENE);
    gr_layer_unref(foreign);
    gr_scene_unref(other);
    d.owned = gr_layer_new(s, GR_LAYER_CONTAINER);
    OK(gr_content_layer_clear_drawer(l));
    CHECK(d.destroys == 1 && !gr_content_layer_has_drawer(l));
    CHECK(gr_scene_get_layer_count(s) == 2);
    OK(gr_content_layer_set_drawer(l, &drawer));
    uint8_t rgba[4] = {0, 0, 0, 0};
    gr_image *image = gr_image_new(1, 1, 1, (gr_bytes){rgba, 4});
    OK(gr_content_layer_set_image(l, image));
    CHECK(d.destroys == 2);
    gr_image_unref(image);
    CHECK(gr_content_layer_get_image(l));
    OK(gr_content_layer_set_drawer(l, &drawer));
    CHECK(!gr_content_layer_get_image(l));
    gr_scene_link *link = gr_scene_get_link(s);
    gr_scene_unref(s);
    CHECK(d.destroys == 3 && !gr_layer_is_alive(l));
    CHECK(!gr_scene_link_lock(link));
    CHECK(gr_drawer_provider_draw(provider, l, version, &info, &list) == GR_ERROR_STATE);
    CHECK(gr_content_layer_set_drawer(l, &drawer) == GR_ERROR_STATE && d.destroys == 3);
    OK(gr_layer_set_opacity(l, 0.2f));
    CHECK(gr_layer_get_opacity(l) == 1);
    gr_drawer_provider_unref(provider);
    gr_scene_link_unref(link);
    gr_layer_unref(l);
}

static void rejected_drawer_preserves_ownership(void)
{
    recorder r;
    gr_scene *s = new_scene(&r);
    gr_layer *content = gr_layer_new(s, GR_LAYER_CONTENT);
    draw_state installed = {.s = s, .l = content}, rejected = {0};
    gr_drawer good = {&installed, draw_callback, draw_destroy};
    gr_drawer bad = {&rejected, NULL, draw_destroy};
    OK(gr_layer_add_child(gr_scene_get_root(s), content));
    OK(gr_content_layer_set_drawer(content, &good));
    OK(gr_scene_commit(s));
    CHECK(gr_content_layer_set_drawer(content, &bad) == GR_ERROR_ARGUMENT);
    bad.draw = draw_callback;
    CHECK(gr_content_layer_set_drawer(gr_scene_get_root(s), &bad) == GR_ERROR_KIND);
    CHECK(installed.destroys == 0 && rejected.destroys == 0);
    CHECK(gr_content_layer_has_drawer(content) && !gr_scene_needs_commit(s));
    const gr_resolved *resolved = &update(&r, content)->resolved;
    gr_draw_info info = gr_resolved_get_draw_info(resolved, 1);
    gr_display_list *list = NULL;
    OK(gr_drawer_provider_draw(r.provider, content, resolved->content.value.display_version, &info, &list));
    CHECK(installed.draws == 1 && rejected.draws == 0);
    gr_display_list_unref(list);
    gr_scene_unref(s);
    CHECK(installed.destroys == 1 && rejected.destroys == 0);
    gr_layer_unref(content);
    /* Failed transfer leaves cleanup to the caller. */
    bad.destroy(bad.userdata);
    CHECK(rejected.destroys == 1);
}

static void geometry(void)
{
    recorder r;
    gr_scene *s = new_scene(&r);
    gr_layer *l = gr_layer_new(s, GR_LAYER_CONTENT);
    OK(gr_layer_add_child(gr_scene_get_root(s), l));
    OK(gr_scene_set_pixel_snapping(s, false));
    OK(gr_layer_set_bounds(l, (gr_rect){{3, 4}, {20, 10}}));
    OK(gr_layer_set_position(l, (gr_point){30, 40}));
    OK(gr_layer_set_transform(l, (gr_affine){2, 0, 0, 3, 1, 2}));
    OK(gr_scene_commit(s));
    const gr_resolved *v = &update(&r, l)->resolved;
    CHECK_NEAR(v->transform.tx, 11);
    CHECK_NEAR(v->transform.ty, 27);
    CHECK_NEAR(v->bounds_offset.x, -3);
    CHECK_NEAR(v->bounds_offset.y, -4);
    OK(gr_layer_set_transform(l, (gr_affine){1, 0, 0, 1, 0, 0}));
    OK(gr_layer_set_frame(l, (gr_rect){{5, 6}, {11, 12}}));
    gr_rect frame = gr_layer_get_frame(l);
    CHECK_NEAR(frame.origin.x, 5);
    CHECK_NEAR(frame.size.height, 12);
    CHECK_NEAR(gr_layer_get_bounds(l).origin.x, 3);
    gr_border border = {0.3, {0, 0, 0, 1}};
    OK(gr_layer_set_border(l, &border));
    gr_shadow shadow = {.color = {0, 0, 0, 1}, .blur_sigma = 2, .spread = 3};
    OK(gr_layer_set_shadow(l, &shadow));
    OK(gr_layer_set_corner_radius(l, 2));
    OK(gr_layer_set_bounds(l, (gr_rect){{0.2, 0.2}, {10.2, 4.2}}));
    OK(gr_scene_set_pixel_snapping(s, true));
    OK(gr_scene_set_backing_scale(s, 1.5));
    OK(gr_scene_commit(s));
    v = &update(&r, l)->resolved;
    CHECK_NEAR(v->size.width, 10);
    CHECK_NEAR(v->size.height, 4);
    CHECK_NEAR(v->border.width, 2.0 / 3);
    gr_path *outline = gr_shadow_shape_new_path(&v->shadow_shape, v->bounds_offset);
    CHECK(outline);
    gr_path_element_list el = gr_path_get_elements(outline);
    CHECK_NEAR(el.data[0].p[0].x, 2);
    CHECK_NEAR(el.data[0].p[0].y, -3);
    gr_path_unref(outline);
    gr_path_element e = {GR_PATH_MOVE, {{4, 5}}};
    gr_path *path = gr_path_new((gr_path_element_list){&e, 1});
    shadow.path = path;
    OK(gr_layer_set_shadow(l, &shadow));
    gr_path_unref(path);
    OK(gr_scene_commit(s));
    OK(gr_layer_set_bounds(l, (gr_rect){{1, 2}, {10.2, 4.2}}));
    OK(gr_scene_commit(s));
    CHECK(update(&r, l)->changed == (GR_DIRTY_GEOMETRY | GR_DIRTY_SHADOW));
    v = &update(&r, l)->resolved;
    outline = gr_shadow_shape_new_path(&v->shadow_shape, v->bounds_offset);
    el = gr_path_get_elements(outline);
    CHECK_NEAR(el.data[0].p[0].x, 4 - 4.0 / 3);
    CHECK_NEAR(el.data[0].p[0].y, 3);
    gr_path_unref(outline);
    CHECK(gr_layer_set_opacity(l, NAN) == GR_ERROR_ARGUMENT);
    CHECK(gr_layer_set_bounds(l, (gr_rect){{0, 0}, {-1, 2}}) == GR_ERROR_ARGUMENT);
    gr_layer_unref(l);
    gr_scene_unref(s);
}

static void cascade(void)
{
    recorder r;
    gr_scene *s = new_scene(&r);
    gr_layer *a = gr_layer_new(s, GR_LAYER_CONTENT), *b = gr_layer_new(s, GR_LAYER_CONTENT);
    draw_state da = {.owned = b}, db = {.owned = gr_layer_new(s, GR_LAYER_CONTAINER)};
    gr_drawer ad = {&da, draw_callback, draw_destroy}, bd = {&db, draw_callback, draw_destroy};
    OK(gr_content_layer_set_drawer(a, &ad));
    OK(gr_content_layer_set_drawer(b, &bd));
    gr_layer_unref(a);
    CHECK(da.destroys == 1 && db.destroys == 1 && gr_scene_get_layer_count(s) == 1);
    gr_scene_unref(s);
}

static void property_dependencies(void)
{
    recorder r;
    gr_scene *s = new_scene(&r);
    gr_layer *l = gr_layer_new(s, GR_LAYER_CONTENT), *shape = gr_layer_new(s, GR_LAYER_SHAPE);
    gr_layer *gradient = gr_layer_new(s, GR_LAYER_GRADIENT);
    OK(gr_layer_add_child(gr_scene_get_root(s), l));
    OK(gr_layer_add_child(gr_scene_get_root(s), shape));
    OK(gr_layer_add_child(gr_scene_get_root(s), gradient));
    uint8_t bytes[16] = {0};
    gr_image *image = gr_image_new(2, 2, 1, (gr_bytes){bytes, 16});
    OK(gr_content_layer_set_image(l, image));
    gr_image_unref(image);
    OK(gr_content_layer_set_gravity(l, GR_GRAVITY_CENTER));
    gr_shadow shadow = {.color = {0, 0, 0, 1}, .blur_sigma = 2};
    OK(gr_layer_set_shadow(l, &shadow));
    gr_paint_desc desc = {.kind = GR_PAINT_SOLID, .value.solid = {1, 0, 0, 1}};
    gr_paint *p = gr_paint_new(&desc), *same = gr_paint_new(&desc);
    gr_stroke_style style = gr_stroke_style_default();
    gr_stroke *stroke = gr_stroke_new(p, &style), *same_stroke = gr_stroke_new(same, &style);
    OK(gr_shape_layer_set_fill(shape, p));
    OK(gr_shape_layer_set_stroke(shape, stroke));
    OK(gr_scene_commit(s));
    OK(gr_shape_layer_set_fill(shape, same));
    OK(gr_shape_layer_set_stroke(shape, same_stroke));
    CHECK(!gr_scene_needs_commit(s));
    CHECK(gr_shape_layer_set_fill(l, p) == GR_ERROR_KIND);
    CHECK(gr_gradient_layer_set_gradient(gradient, p) == GR_ERROR_ARGUMENT);
    gr_paint_unref(p);
    gr_paint_unref(same);
    gr_stroke_unref(stroke);
    gr_stroke_unref(same_stroke);
    OK(gr_layer_set_bounds(l, (gr_rect){{0, 0}, {20, 10}}));
    OK(gr_scene_commit(s));
    CHECK(update(&r, l)->changed == (GR_DIRTY_GEOMETRY | GR_DIRTY_SHADOW | GR_DIRTY_CONTENT));
    CHECK_NEAR(update(&r, l)->resolved.content.value.image.rect.origin.x, 9);
    OK(gr_layer_set_corner_radius(l, 2));
    OK(gr_scene_commit(s));
    CHECK(update(&r, l)->changed == (GR_DIRTY_APPEARANCE | GR_DIRTY_SHADOW));
    OK(gr_scene_set_pixel_snapping(s, false));
    OK(gr_scene_commit(s));
    OK(gr_scene_set_backing_scale(s, 2));
    OK(gr_scene_commit(s));
    CHECK(update(&r, l)->changed == GR_DIRTY_CONTENT);
    CHECK(update(&r, shape)->changed == GR_DIRTY_CONTENT);
    CHECK(update(&r, gradient)->changed == GR_DIRTY_CONTENT);
    CHECK(!update(&r, gr_scene_get_root(s)));
    gr_gradient_stop stops[] = {{0, {0, 0, 0, 1}}, {1, {1, 1, 1, 1}}};
    desc.kind = GR_PAINT_LINEAR;
    desc.value.linear.start = (gr_point){0, 0};
    desc.value.linear.end = (gr_point){1, 1};
    desc.value.linear.stops = (gr_gradient_stop_list){stops, 2};
    p = gr_paint_new(&desc);
    same = gr_paint_new(&desc);
    OK(gr_gradient_layer_set_gradient(gradient, p));
    OK(gr_scene_commit(s));
    OK(gr_gradient_layer_set_gradient(gradient, same));
    CHECK(!gr_scene_needs_commit(s));
    gr_paint_unref(p);
    gr_paint_unref(same);
    for (int gravity = GR_GRAVITY_CENTER; gravity <= GR_GRAVITY_ASPECT_FILL; ++gravity)
    {
        OK(gr_content_layer_set_gravity(l, (gr_gravity)gravity));
        OK(gr_scene_commit(s));
        if (update(&r, l))
            CHECK(isfinite(update(&r, l)->resolved.content.value.image.rect.size.width));
    }
    gr_layer_unref(l);
    gr_layer_unref(shape);
    gr_layer_unref(gradient);
    gr_scene_unref(s);
}

static gr_layer *release_in_apply;

static void release_callback(recorder *r, const gr_commit_batch *batch)
{
    (void)batch;
    gr_layer_unref(release_in_apply);
    release_in_apply = NULL;
    r->during_apply = NULL;
}

static void deferred_release(void)
{
    recorder r;
    gr_scene *s = new_scene(&r);
    gr_layer *l = gr_layer_new(s, GR_LAYER_CONTENT);
    OK(gr_layer_add_child(gr_scene_get_root(s), l));
    OK(gr_scene_commit(s));
    OK(gr_layer_remove_from_parent(l));
    OK(gr_scene_commit(s));
    release_in_apply = l;
    r.during_apply = release_callback;
    OK(gr_layer_set_hidden(gr_scene_get_root(s), true));
    size_t requests = r.requests;
    OK(gr_scene_commit(s));
    CHECK(r.released == 0 && r.requests == requests + 1 && gr_scene_needs_commit(s));
    OK(gr_scene_commit(s));
    CHECK(r.released == 1 && !gr_scene_needs_commit(s));
    gr_scene_unref(s);
}

typedef struct
{
    gr_scene *owned;
    gr_layer *observer;
    size_t calls;
} cleanup_state;

static void no_draw(void *userdata, gr_canvas *canvas, const gr_draw_info *info)
{
    (void)userdata;
    (void)canvas;
    (void)info;
}

static void cleanup_reenter(void *userdata)
{
    cleanup_state *c = userdata;
    ++c->calls;
    if (gr_layer_is_alive(c->observer))
        OK(gr_layer_set_opacity(c->observer, 0.25f));
    gr_scene_unref(c->owned);
    c->owned = NULL;
}

static void teardown_reentry(void)
{
    recorder r;
    gr_scene *s = new_scene(&r);
    gr_layer *l = gr_layer_new(s, GR_LAYER_CONTENT), *observer = gr_layer_new(s, GR_LAYER_CONTAINER);
    cleanup_state state = {s, observer, 0};
    gr_drawer drawer = {&state, no_draw, cleanup_reenter};
    OK(gr_content_layer_set_drawer(l, &drawer));
    OK(gr_layer_add_child(gr_scene_get_root(s), l));
    gr_layer_unref(l);
    /* Only the callback owns the Scene reference; remove must protect its call. */
    l = gr_layer_get_child(gr_scene_get_root(s), 0);
    OK(gr_layer_remove_from_parent(l));
    CHECK(state.calls == 1 && r.destroys == 1 && !gr_layer_is_alive(observer));
    gr_layer_unref(observer);

    s = new_scene(&r);
    l = gr_layer_new(s, GR_LAYER_CONTENT);
    observer = gr_layer_new(s, GR_LAYER_CONTAINER);
    state = (cleanup_state){NULL, observer, 0};
    drawer = (gr_drawer){&state, no_draw, cleanup_reenter};
    OK(gr_content_layer_set_drawer(l, &drawer));
    OK(gr_layer_add_child(l, observer));
    OK(gr_layer_add_child(gr_scene_get_root(s), l));
    gr_scene_link *link = gr_scene_get_link(s);
    gr_scene_unref(s);
    CHECK(state.calls == 1 && !gr_scene_link_lock(link));
    CHECK(!gr_layer_get_parent(observer) && gr_layer_get_child_count(l) == 0);
    gr_layer_unref(l);
    gr_layer_unref(observer);
    gr_scene_link_unref(link);
}

static void unavailable_backend(void)
{
    gr_scene *s = gr_scene_new((gr_size){0, 0});
    CHECK(gr_scene_commit(s) == GR_ERROR_STATE && gr_scene_needs_commit(s));
    gr_backend_vtable bad = {0};
    CHECK(gr_scene_set_backend(s, &bad, NULL) == GR_ERROR_ARGUMENT);
    recorder r;
    init_recorder(&r, s);
    gr_backend_vtable v = {request, apply, destroy};
    OK(gr_scene_set_backend(s, &v, &r));
    CHECK(gr_scene_set_backend(s, &v, &r) == GR_ERROR_STATE);
    OK(gr_scene_commit(s));
    CHECK(r.realized == 1);
    gr_scene_unref(s);
    CHECK(r.destroys == 1);
}

static void deep_tree(void)
{
    gr_scene *s = gr_scene_new((gr_size){10, 10});
    gr_layer *top = gr_layer_new(s, GR_LAYER_CONTAINER);
    gr_layer *parent = top;
    for (size_t i = 0; i < 4096; ++i)
    {
        gr_layer *child = gr_layer_new(s, GR_LAYER_CONTAINER);
        OK(gr_layer_add_child(parent, child));
        if (parent != top)
            gr_layer_unref(parent);
        parent = child;
    }
    gr_layer_unref(parent);
    CHECK(gr_scene_get_layer_count(s) == 4098);
    OK(gr_layer_add_child(gr_scene_get_root(s), top));
    OK(gr_layer_remove_from_parent(top));
    gr_layer_unref(top);
    CHECK(gr_scene_get_layer_count(s) == 1);
    gr_scene_unref(s);
}

static void borrowed_reparent_and_reorder(void)
{
    recorder r;
    gr_scene *s = new_scene(&r);
    gr_layer *root = gr_scene_get_root(s);
    gr_layer *a = gr_layer_new(s, GR_LAYER_CONTAINER), *b = gr_layer_new(s, GR_LAYER_CONTAINER);
    gr_layer *child = gr_layer_new(s, GR_LAYER_CONTAINER);
    OK(gr_layer_add_child(root, a));
    OK(gr_layer_add_child(root, b));
    OK(gr_layer_add_child(a, child));
    gr_layer_unref(child);
    OK(gr_scene_commit(s));
    child = gr_layer_get_child(a, 0);
    /* Reparent with only the old parent's reference keeping the child alive. */
    OK(gr_layer_add_child(b, child));
    CHECK(gr_layer_get_child_count(a) == 0 && gr_layer_get_parent(child) == b);
    OK(gr_scene_commit(s));
    CHECK(r.realized == 0 && r.released == 0 && node(&r, child)->parent == b);
    OK(gr_layer_insert_child(root, b, 0));
    OK(gr_scene_commit(s));
    CHECK(node(&r, root)->children[0] == b && node(&r, root)->children[1] == a);
    OK(gr_layer_insert_child(root, b, SIZE_MAX));
    OK(gr_scene_commit(s));
    CHECK(node(&r, root)->count == 2);
    CHECK(node(&r, root)->children[0] == a && node(&r, root)->children[1] == b);
    CHECK(r.realized == 0 && r.released == 0);
    OK(gr_layer_remove_from_parent(child));
    CHECK(gr_scene_get_layer_count(s) == 3);
    OK(gr_scene_commit(s));
    CHECK(r.released == 1 && node(&r, b)->count == 0);
    gr_layer_unref(a);
    gr_layer_unref(b);
    gr_scene_unref(s);
}

static void rejected_mutations_stay_clean(void)
{
    recorder r;
    gr_scene *s = new_scene(&r), *other = gr_scene_new((gr_size){1, 1});
    gr_layer *root = gr_scene_get_root(s), *a = gr_layer_new(s, GR_LAYER_CONTAINER);
    gr_layer *child = gr_layer_new(s, GR_LAYER_CONTAINER), *mask = gr_layer_new(s, GR_LAYER_SHAPE);
    OK(gr_layer_add_child(root, a));
    OK(gr_layer_add_child(a, child));
    OK(gr_layer_set_mask(a, mask));
    OK(gr_layer_set_position(a, (gr_point){12, 34}));
    OK(gr_scene_commit(s));
    size_t requests = r.requests, applies = r.applies;
    CHECK(gr_layer_add_child(child, a) == GR_ERROR_CYCLE);
    CHECK(gr_layer_add_child(gr_scene_get_root(other), child) == GR_ERROR_SCENE);
    CHECK(gr_layer_add_child(root, mask) == GR_ERROR_STATE);
    CHECK(gr_layer_set_mask(a, gr_scene_get_root(other)) == GR_ERROR_SCENE);
    CHECK(gr_layer_set_mask(a, a) == GR_ERROR_CYCLE);
    CHECK(gr_layer_set_mask(a, child) == GR_ERROR_STATE);
    CHECK(gr_layer_set_position(a, (gr_point){NAN, 0}) == GR_ERROR_ARGUMENT);
    CHECK(gr_layer_set_corner_radius(a, -1) == GR_ERROR_ARGUMENT);
    CHECK(gr_layer_set_masked_corners(a, (gr_corners)0x80) == GR_ERROR_ARGUMENT);
    CHECK(gr_scene_set_backing_scale(s, 0) == GR_ERROR_ARGUMENT);
    CHECK(gr_layer_get_parent(child) == a && gr_layer_get_parent(a) == root);
    CHECK(gr_layer_get_child_count(a) == 1 && gr_layer_get_mask(a) == mask);
    CHECK(gr_layer_get_position(a).x == 12 && gr_layer_get_position(a).y == 34);
    CHECK(gr_layer_get_corner_radius(a) == 0 && gr_layer_get_masked_corners(a) == GR_CORNERS_ALL);
    CHECK(gr_scene_get_backing_scale(s) == 1);
    CHECK(!gr_scene_needs_commit(s) && r.requests == requests);
    OK(gr_scene_commit(s));
    CHECK(r.applies == applies);
    gr_layer_unref(a);
    gr_layer_unref(child);
    gr_layer_unref(mask);
    gr_scene_unref(other);
    gr_scene_unref(s);
}

static void mask_replacement_lifetime(void)
{
    recorder r;
    gr_scene *s = new_scene(&r);
    gr_layer *owner = gr_layer_new(s, GR_LAYER_CONTAINER);
    gr_layer *old = gr_layer_new(s, GR_LAYER_SHAPE), *replacement = gr_layer_new(s, GR_LAYER_SHAPE);
    OK(gr_layer_add_child(gr_scene_get_root(s), owner));
    OK(gr_layer_set_mask(owner, old));
    gr_layer_unref(old);
    OK(gr_scene_commit(s));
    CHECK(update(&r, owner)->resolved.mask == gr_layer_get_mask(owner));
    OK(gr_layer_set_mask(owner, replacement));
    CHECK(gr_scene_get_layer_count(s) == 3);
    gr_layer_unref(replacement);
    OK(gr_scene_commit(s));
    CHECK(r.realized == 1 && r.released == 1);
    CHECK(update(&r, owner)->changed == GR_DIRTY_MASK);
    CHECK(update(&r, owner)->resolved.mask == gr_layer_get_mask(owner));
    CHECK(node(&r, gr_layer_get_mask(owner)));
    OK(gr_layer_set_mask(owner, gr_layer_get_mask(owner)));
    CHECK(!gr_scene_needs_commit(s));
    OK(gr_layer_set_mask(owner, NULL));
    CHECK(gr_scene_get_layer_count(s) == 2);
    OK(gr_scene_commit(s));
    CHECK(r.realized == 0 && r.released == 1 && !update(&r, owner)->resolved.mask);
    gr_layer_unref(owner);
    gr_scene_unref(s);
}

void graphite_lifecycle_example(void);

int main(int argc, char **argv)
{
    const gr_test_case cases[] = {
        GR_TEST_CASE(basics),
        GR_TEST_CASE(tree),
        GR_TEST_CASE(borrowed_reparent_and_reorder),
        GR_TEST_CASE(rejected_mutations_stay_clean),
        GR_TEST_CASE(mask_replacement_lifetime),
        GR_TEST_CASE(drawers),
        GR_TEST_CASE(rejected_drawer_preserves_ownership),
        GR_TEST_CASE(geometry),
        GR_TEST_CASE(cascade),
        GR_TEST_CASE(property_dependencies),
        GR_TEST_CASE(deferred_release),
        GR_TEST_CASE(teardown_reentry),
        GR_TEST_CASE(unavailable_backend),
        GR_TEST_CASE(deep_tree),
        GR_TEST_CASE(graphite_lifecycle_example),
    };
    return gr_test_run("scene", cases, sizeof(cases) / sizeof(cases[0]), argc, argv);
}
