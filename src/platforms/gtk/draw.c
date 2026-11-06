#include "internal.h"

void gr_gsk_paint(GtkSnapshot *s, gr_paint *paint, const graphene_rect_t *bounds, gr_size units)
{
    gr_paint_desc p = gr_paint_get_desc(paint);
    if (p.kind == GR_PAINT_SOLID)
    {
        GdkRGBA color = gr_gdk_color(p.value.solid);
        gtk_snapshot_append_color(s, &color, bounds);
        return;
    }
    gr_gradient_stop_list input = p.kind == GR_PAINT_LINEAR ? p.value.linear.stops : p.value.radial.stops;
    GskColorStop *stops = g_new(GskColorStop, input.count);
    for (size_t i = 0; i < input.count; ++i)
        stops[i] = (GskColorStop){input.data[i].offset, gr_gdk_color(input.data[i].color)};
    if (p.kind == GR_PAINT_LINEAR)
    {
        graphene_point_t from = {(float)(p.value.linear.start.x * units.width),
                                 (float)(p.value.linear.start.y * units.height)};
        graphene_point_t to = {(float)(p.value.linear.end.x * units.width),
                               (float)(p.value.linear.end.y * units.height)};
        gtk_snapshot_append_linear_gradient(s, bounds, &from, &to, stops, input.count);
    }
    else
    {
        graphene_point_t center = {(float)(p.value.radial.center.x * units.width),
                                   (float)(p.value.radial.center.y * units.height)};
        gtk_snapshot_append_radial_gradient(s, bounds, &center, (float)(p.value.radial.radius * units.width),
                                            (float)(p.value.radial.radius * units.height), 0, 1, stops, input.count);
    }
    g_free(stops);
}

void gr_gsk_paint_path(GtkSnapshot *s, GskPath *path, gr_fill_rule rule, GskStroke *stroke, gr_paint *paint)
{
    gr_paint_desc p = gr_paint_get_desc(paint);
    GskFillRule fill = rule == GR_FILL_EVEN_ODD ? GSK_FILL_RULE_EVEN_ODD : GSK_FILL_RULE_WINDING;
    if (p.kind == GR_PAINT_SOLID)
    {
        GdkRGBA color = gr_gdk_color(p.value.solid);
        if (stroke)
            gtk_snapshot_append_stroke(s, path, stroke, &color);
        else
            gtk_snapshot_append_fill(s, path, fill, &color);
        return;
    }
    graphene_rect_t bounds;
    bool valid = stroke ? gsk_path_get_stroke_bounds(path, stroke, &bounds) : gsk_path_get_bounds(path, &bounds);
    if (!valid)
        return;
    if (stroke)
        gtk_snapshot_push_stroke(s, path, stroke);
    else
        gtk_snapshot_push_fill(s, path, fill);
    gr_gsk_paint(s, paint, &bounds, (gr_size){1, 1});
    gtk_snapshot_pop(s);
}

typedef struct
{
    double alpha;
    size_t pushes;
} draw_state;

GskRenderNode *gr_gsk_recording(const gr_display_list *list)
{
    GtkSnapshot *s = gtk_snapshot_new();
    gr_draw_op_list ops = gr_display_list_get_ops(list);
    draw_state *stack = g_new0(draw_state, ops.count + 1);
    size_t depth = 0;
    stack[0].alpha = 1;
    for (size_t i = 0; i < ops.count; ++i)
    {
        const gr_draw_op *op = &ops.data[i];
        draw_state *top = &stack[depth];
        switch (op->kind)
        {
            case GR_DRAW_SAVE:
                gtk_snapshot_save(s);
                stack[depth + 1] = (draw_state){top->alpha, 0};
                ++depth;
                break;
            case GR_DRAW_RESTORE:
                if (depth)
                {
                    for (size_t j = 0; j < top->pushes; ++j)
                        gtk_snapshot_pop(s);
                    --depth;
                    gtk_snapshot_restore(s);
                }
                break;
            case GR_DRAW_TRANSFORM:
            {
                GskTransform *t = gr_gsk_transform(op->value.transform);
                gtk_snapshot_transform(s, t);
                if (t)
                    gsk_transform_unref(t);
                break;
            }
            case GR_DRAW_ALPHA:
                top->alpha *= op->value.alpha;
                break;
            case GR_DRAW_CLIP_RECT:
            {
                graphene_rect_t r = gr_graphene_rect(op->value.clip_rect);
                gtk_snapshot_push_clip(s, &r);
                ++top->pushes;
                break;
            }
            case GR_DRAW_CLIP_PATH:
            {
                GskPath *p = gr_gsk_path(op->value.clip_path.path);
                GdkRGBA black = {0, 0, 0, 1};
                gtk_snapshot_push_mask(s, GSK_MASK_MODE_ALPHA);
                gtk_snapshot_append_fill(
                    s, p, op->value.clip_path.rule == GR_FILL_EVEN_ODD ? GSK_FILL_RULE_EVEN_ODD : GSK_FILL_RULE_WINDING,
                    &black);
                gtk_snapshot_pop(s);
                gsk_path_unref(p);
                ++top->pushes;
                break;
            }
            case GR_DRAW_FILL_PATH:
            case GR_DRAW_STROKE_PATH:
            case GR_DRAW_IMAGE:
                if (top->alpha < 1)
                    gtk_snapshot_push_opacity(s, top->alpha);
                if (op->kind == GR_DRAW_FILL_PATH)
                {
                    GskPath *p = gr_gsk_path(op->value.fill_path.path);
                    gr_gsk_paint_path(s, p, op->value.fill_path.rule, NULL, op->value.fill_path.paint);
                    gsk_path_unref(p);
                }
                else if (op->kind == GR_DRAW_STROKE_PATH)
                {
                    GskPath *p = gr_gsk_path(op->value.stroke_path.path);
                    gr_stroke_style style = gr_stroke_get_style(op->value.stroke_path.stroke);
                    GskStroke *stroke = gr_gsk_stroke(&style);
                    gr_gsk_paint_path(s, p, GR_FILL_NONZERO, stroke, gr_stroke_get_paint(op->value.stroke_path.stroke));
                    gsk_stroke_free(stroke);
                    gsk_path_unref(p);
                }
                else
                {
                    GdkTexture *texture = gr_gdk_texture(op->value.image.image);
                    graphene_rect_t r = gr_graphene_rect(op->value.image.destination);
                    gtk_snapshot_append_scaled_texture(s, texture, GSK_SCALING_FILTER_LINEAR, &r);
                    g_object_unref(texture);
                }
                if (top->alpha < 1)
                    gtk_snapshot_pop(s);
                break;
        }
    }
    for (size_t i = 0; i < stack[0].pushes; ++i)
        gtk_snapshot_pop(s);
    g_free(stack);
    return gtk_snapshot_free_to_node(s);
}
