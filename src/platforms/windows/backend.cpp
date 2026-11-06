#include "internal.hpp"

namespace gr_win
{
static void detach(Visual const &visual)
{
    if (auto parent = visual.Parent())
        parent.Children().Remove(visual);
}

static uint32_t pixel_dimension(double value)
{
    value = std::max(1.0, std::ceil(value));
    if (!std::isfinite(value) || value > INT_MAX)
        throw_hresult(E_INVALIDARG);
    return static_cast<uint32_t>(value);
}

static bool same_rect(gr_rect a, gr_rect b)
{
    return a.origin.x == b.origin.x && a.origin.y == b.origin.y && a.size.width == b.size.width &&
           a.size.height == b.size.height;
}

static bool same_shadow(gr_shadow_shape const &a, gr_shadow_shape const &b)
{
    if (a.kind != b.kind)
        return false;
    if (a.kind == GR_SHADOW_PATH)
        return a.value.path == b.value.path;
    auto &x = a.value.rounded_rect;
    auto &y = b.value.rounded_rect;
    return same_rect(x.rect, y.rect) && x.radius == y.radius && x.corners == y.corners && x.spread == y.spread;
}

struct ShapeParts
{
    ShapeVisual visual{nullptr};
    CompositionContainerShape container{nullptr};
    CompositionSpriteShape fill{nullptr}, stroke{nullptr};
    gr_rect extent{};
};

struct ShadowParts
{
    SpriteVisual sprite{nullptr};
    DropShadow drop{nullptr};
    CompositionDrawingSurface surface{nullptr};
    bool valid = false;
    gr_shadow_shape shape{};
    gr_point offset{};
    double scale = 0;

    ~ShadowParts()
    {
        if (valid && shape.kind == GR_SHADOW_PATH)
            gr_path_unref(shape.value.path);
    }

    void remember(gr_shadow_shape const &next, gr_point at, double factor)
    {
        if (next.kind == GR_SHADOW_PATH)
            gr_path_ref(next.value.path);
        if (valid && shape.kind == GR_SHADOW_PATH)
            gr_path_unref(shape.value.path);
        shape = next;
        offset = at;
        scale = factor;
        valid = true;
    }
};

struct Masking
{
    LayerVisual layer{nullptr};
    ContainerVisual host{nullptr};
    CompositionVisualSurface surface{nullptr};
};

struct Drawing
{
    uint64_t version = 0;
    gr_rect bounds{};
    double scale = 0;
    uint32_t width = 0, height = 0;
    CompositionDrawingSurface surface{nullptr};
};

struct Node
{
    ContainerVisual outer{nullptr}, body{nullptr}, clipped{nullptr}, sublayers{nullptr};
    SpriteVisual background{nullptr}, sprite{nullptr};
    std::unique_ptr<ShapeParts> shape;
    ShapeVisual border_visual{nullptr};
    CompositionSpriteShape border_shape{nullptr};
    std::unique_ptr<ShadowParts> shadow;
    std::unique_ptr<Masking> masking;
    std::unique_ptr<Drawing> drawing;
    uint64_t image = 0;
    gr_layer *parent = nullptr, *mask = nullptr, *mask_owner = nullptr;
    std::vector<gr_layer *> children;
};

struct ImageEntry
{
    CompositionDrawingSurface surface{nullptr};
    size_t uses = 0;
};

struct Backend
{
    Context &ctx;
    ContainerVisual scaled{nullptr};
    std::shared_ptr<gr_scene_link> scene;
    std::unordered_map<gr_layer *, std::unique_ptr<Node>> nodes;
    std::unordered_map<uint64_t, ImageEntry> images;
    gr_drawer_provider *drawers = nullptr;
    double scale = 1;
    gr_layer *root = nullptr;
    bool reported = false;

    Backend(gr_scene *s, ContainerVisual const &v)
        : ctx(context()), scaled(v), scene(gr_scene_get_link(s), gr_scene_link_unref)
    {
    }

    ~Backend()
    {
        try
        {
            scaled.Children().RemoveAll();
            for (auto &entry : nodes)
                detach(entry.second->outer);
        }
        catch (...)
        {
        }
        gr_drawer_provider_unref(drawers);
    }

    Node &node(gr_layer *id)
    {
        return *nodes.at(id);
    }

    Compositor const &comp()
    {
        return ctx.compositor;
    }

    void error(char const *operation)
    {
        if (!reported)
        {
            reported = true;
            report(operation);
        }
    }

    void clear_content(Node &n)
    {
        if (n.sprite)
        {
            detach(n.sprite);
            n.sprite = nullptr;
        }
        if (n.shape)
        {
            detach(n.shape->visual);
            n.shape.reset();
        }
    }

    SpriteVisual content_sprite(Node &n)
    {
        if (n.sprite)
            return n.sprite;
        clear_content(n);
        n.sprite = comp().CreateSpriteVisual();
        n.clipped.Children().InsertBelow(n.sprite, n.sublayers);
        return n.sprite;
    }

    void unmask(Node &n)
    {
        if (!n.masking)
            return;
        n.masking->host.Children().RemoveAll();
        n.masking->layer.Children().RemoveAll();
        n.outer.Children().Remove(n.masking->layer);
        n.outer.Children().InsertAtTop(n.body);
        n.masking.reset();
    }

    void clear_mask_owner(gr_layer *id)
    {
        auto &n = node(id);
        if (n.mask_owner)
        {
            auto &owner = node(n.mask_owner);
            owner.mask = nullptr;
            unmask(owner);
            n.mask_owner = nullptr;
        }
        detach(n.outer);
    }

    CompositionBrush paint_brush(gr_paint *p, bool relative);
    CompositionClip rounded_clip(gr_rect r, double radius, gr_corners corners);
    void decoration(Node &n, gr_resolved const &r);
    void shadow(Node &n, gr_resolved const &r);
    void raster_shadow(Node &n, gr_shadow_shape const &shape, gr_point offset);
    void apply_mask(gr_layer *id, gr_resolved const &r);
    void update_shape(Node &n, gr_shape_props const &shape, gr_resolved const &r, bool changed);
    void content(gr_layer *id, gr_resolved const &r, bool changed);
    void drawing(gr_layer *id, gr_resolved const &r);
    void update(gr_layer_update const &u);
    void release(gr_layer *id);
    void apply(gr_commit_batch const &batch);
    CompositionDrawingSurface acquire_image(gr_image *image);

    void release_image(uint64_t id)
    {
        auto it = images.find(id);
        if (it != images.end() && !--it->second.uses)
            images.erase(it);
    }
};

CompositionBrush Backend::paint_brush(gr_paint *paint, bool relative)
{
    auto p = gr_paint_get_desc(paint);
    if (p.kind == GR_PAINT_SOLID)
        return comp().CreateColorBrush(color(p.value.solid));
    CompositionGradientBrush brush{nullptr};
    auto mode = relative ? CompositionMappingMode::Relative : CompositionMappingMode::Absolute;
    gr_gradient_stop_list stops;
    if (p.kind == GR_PAINT_LINEAR)
    {
        auto b = comp().CreateLinearGradientBrush();
        b.MappingMode(mode);
        b.StartPoint(vec(p.value.linear.start.x, p.value.linear.start.y));
        b.EndPoint(vec(p.value.linear.end.x, p.value.linear.end.y));
        stops = p.value.linear.stops;
        brush = b;
    }
    else
    {
        auto b = comp().CreateRadialGradientBrush();
        b.MappingMode(mode);
        b.EllipseCenter(vec(p.value.radial.center.x, p.value.radial.center.y));
        b.EllipseRadius(vec(p.value.radial.radius, p.value.radial.radius));
        b.GradientOriginOffset({0, 0});
        stops = p.value.radial.stops;
        brush = b;
    }
    for (size_t i = 0; i < stops.count; ++i)
        brush.ColorStops().Append(comp().CreateColorGradientStop(stops.data[i].offset, color(stops.data[i].color)));
    brush.ExtendMode(CompositionGradientExtendMode::Clamp);
    brush.InterpolationSpace(CompositionColorSpace::Rgb);
    return brush;
}

CompositionClip Backend::rounded_clip(gr_rect r, double radius, gr_corners corners)
{
    double rad = std::min(std::max(radius, 0.0), std::min(std::abs(r.size.width), std::abs(r.size.height)) / 2);
    auto at = [&](gr_corners c)
    {
        double v = corners & c ? rad : 0;
        return vec(v, v);
    };
    return comp().CreateRectangleClip(static_cast<float>(r.origin.x), static_cast<float>(r.origin.y),
                                      static_cast<float>(r.origin.x + r.size.width),
                                      static_cast<float>(r.origin.y + r.size.height), at(GR_CORNER_TOP_LEFT),
                                      at(GR_CORNER_TOP_RIGHT), at(GR_CORNER_BOTTOM_RIGHT), at(GR_CORNER_BOTTOM_LEFT));
}

void Backend::decoration(Node &n, gr_resolved const &r)
{
    auto &p = r.props;
    bool has_border = r.has_border && r.border.width > 0;
    double amount = has_border && r.border.color.a >= 1 ? r.border.width / 2 : 0;
    gr_rect inner = inset({{0, 0}, r.size}, amount);
    double radius = p.corner_radius > 0 ? std::max(0.0, p.corner_radius - amount) : 0;
    bool rounded = radius > 0 && p.masked_corners && inner.size.width > 0 && inner.size.height > 0;
    n.clipped.Clip(p.masks_to_bounds ? rounded_clip(inner, radius, p.masked_corners) : nullptr);
    if (p.has_background)
    {
        if (!n.background)
        {
            n.background = comp().CreateSpriteVisual();
            n.clipped.Children().InsertAtBottom(n.background);
        }
        n.background.Brush(comp().CreateColorBrush(color(p.background)));
        n.background.Offset({static_cast<float>(inner.origin.x), static_cast<float>(inner.origin.y), 0});
        n.background.Size(vec(inner.size.width, inner.size.height));
        n.background.Clip(rounded ? rounded_clip({{0, 0}, inner.size}, radius, p.masked_corners) : nullptr);
    }
    else if (n.background)
    {
        detach(n.background);
        n.background = nullptr;
    }
    if (has_border)
    {
        if (!n.border_visual)
        {
            n.border_visual = comp().CreateShapeVisual();
            n.border_shape = comp().CreateSpriteShape();
            n.border_visual.Shapes().Append(n.border_shape);
            n.body.Children().InsertAtTop(n.border_visual);
        }
        auto b = gr_path_builder_new();
        gr_path_builder_add_rounded_rect(b, {{0, 0}, r.size}, p.corner_radius, p.masked_corners);
        gr_path_builder_add_rounded_rect(b, inset({{0, 0}, r.size}, r.border.width),
                                         std::max(0.0, p.corner_radius - r.border.width), p.masked_corners);
        path_owner ring(gr_path_builder_finish(b), gr_path_unref);
        gr_path_builder_unref(b);
        n.border_shape.Geometry(
            comp().CreatePathGeometry(composition_path(ctx.factory.get(), ring.get(), GR_FILL_EVEN_ODD)));
        n.border_shape.FillBrush(comp().CreateColorBrush(color(r.border.color)));
        n.border_visual.Size(vec(r.size.width, r.size.height));
    }
    else if (n.border_visual)
    {
        detach(n.border_visual);
        n.border_visual = nullptr;
        n.border_shape = nullptr;
    }
}

void Backend::raster_shadow(Node &n, gr_shadow_shape const &shape, gr_point offset)
{
    auto &p = *n.shadow;
    if (p.valid && p.scale == scale && p.offset.x == offset.x && p.offset.y == offset.y && same_shadow(p.shape, shape))
        return;
    path_owner outline(gr_shadow_shape_new_path(&shape, offset), gr_path_unref);
    auto bbox = control_bounds(outline.get());
    if (!bbox)
    {
        p.sprite.Size({0, 0});
        p.remember(shape, offset, scale);
        return;
    }
    auto area = inset(*bbox, -1);
    auto w = pixel_dimension(area.size.width * scale), h = pixel_dimension(area.size.height * scale);
    auto surface = p.surface;
    if (!surface || surface.SizeInt32().Width != static_cast<int32_t>(w) ||
        surface.SizeInt32().Height != static_cast<int32_t>(h))
        surface = ctx.surface(w, h);
    ctx.draw(surface,
             [&](ID2D1DeviceContext *dc, POINT at)
             {
                 gr_affine base{scale, 0, 0, scale, at.x - area.origin.x * scale, at.y - area.origin.y * scale};
                 auto c = gr_canvas_new();
                 gr_paint_desc desc{};
                 desc.kind = GR_PAINT_SOLID;
                 desc.value.solid = {1, 1, 1, 1};
                 auto white = gr_paint_new(&desc);
                 gr_canvas_fill_path(c, outline.get(), GR_FILL_NONZERO, white);
                 gr_paint_unref(white);
                 list_owner list(gr_canvas_finish(c), gr_display_list_unref);
                 gr_canvas_unref(c);
                 replay(dc, ctx.factory.get(), list.get(), base);
             });
    auto brush = comp().CreateSurfaceBrush(surface);
    brush.Stretch(CompositionStretch::Fill);
    p.drop.Mask(brush);
    p.sprite.Offset({static_cast<float>(area.origin.x), static_cast<float>(area.origin.y), 0});
    p.sprite.Size(vec(w / scale, h / scale));
    p.surface = surface;
    p.remember(shape, offset, scale);
}

void Backend::shadow(Node &n, gr_resolved const &r)
{
    if (!r.props.has_shadow || !r.has_shadow)
    {
        if (n.shadow)
        {
            detach(n.shadow->sprite);
            n.shadow.reset();
        }
        return;
    }
    if (!n.shadow)
    {
        n.shadow = std::make_unique<ShadowParts>();
        n.shadow->sprite = comp().CreateSpriteVisual();
        n.shadow->drop = comp().CreateDropShadow();
        n.shadow->sprite.Shadow(n.shadow->drop);
        n.outer.Children().InsertAtBottom(n.shadow->sprite);
    }
    auto s = r.props.shadow;
    auto opaque = s.color;
    opaque.a = 1;
    n.shadow->drop.Color(color(opaque));
    n.shadow->drop.Opacity(s.color.a);
    n.shadow->drop.Offset({static_cast<float>(s.offset.x), static_cast<float>(s.offset.y), 0});
    n.shadow->drop.BlurRadius(static_cast<float>(s.blur_sigma * 3));
    raster_shadow(n, r.shadow_shape, r.bounds_offset);
}

void Backend::apply_mask(gr_layer *id, gr_resolved const &r)
{
    auto &n = node(id);
    if (n.mask == r.mask)
        return;
    if (n.mask)
    {
        auto it = nodes.find(n.mask);
        if (it != nodes.end())
        {
            it->second->mask_owner = nullptr;
            detach(it->second->outer);
        }
    }
    n.mask = r.mask;
    if (!r.mask)
    {
        unmask(n);
        return;
    }
    clear_mask_owner(r.mask);
    auto &mask = node(r.mask);
    mask.parent = nullptr;
    mask.mask_owner = id;
    if (!n.masking)
    {
        n.masking = std::make_unique<Masking>();
        auto &m = *n.masking;
        m.host = comp().CreateContainerVisual();
        m.surface = comp().CreateVisualSurface();
        m.surface.SourceVisual(m.host);
        auto brush = comp().CreateSurfaceBrush(m.surface);
        brush.Stretch(CompositionStretch::Fill);
        auto effect = comp().CreateEffectFactory(alpha_mask()).CreateBrush();
        effect.SetSourceParameter(L"mask", brush);
        m.layer = comp().CreateLayerVisual();
        m.layer.Effect(effect);
        n.outer.Children().Remove(n.body);
        m.layer.Children().InsertAtTop(n.body);
        n.outer.Children().InsertAtTop(m.layer);
    }
    n.masking->host.Children().RemoveAll();
    n.masking->host.Children().InsertAtTop(mask.outer);
}

void Backend::update_shape(Node &n, gr_shape_props const &s, gr_resolved const &r, bool changed)
{
    if (!s.path)
    {
        clear_content(n);
        return;
    }
    if (changed || !n.shape)
    {
        if (!n.shape)
        {
            clear_content(n);
            n.shape = std::make_unique<ShapeParts>();
            auto &p = *n.shape;
            p.visual = comp().CreateShapeVisual();
            p.container = comp().CreateContainerShape();
            p.fill = comp().CreateSpriteShape();
            p.stroke = comp().CreateSpriteShape();
            p.container.Shapes().Append(p.fill);
            p.container.Shapes().Append(p.stroke);
            p.visual.Shapes().Append(p.container);
            n.clipped.Children().InsertBelow(p.visual, n.sublayers);
        }
        auto &p = *n.shape;
        p.fill.Geometry(comp().CreatePathGeometry(composition_path(ctx.factory.get(), s.path, s.fill_rule)));
        p.fill.FillBrush(s.fill ? paint_brush(s.fill, false) : nullptr);
        double margin = 1;
        if (s.stroke)
        {
            auto st = gr_stroke_get_style(s.stroke);
            auto geometry = comp().CreatePathGeometry(composition_path(ctx.factory.get(), s.path, GR_FILL_NONZERO));
            geometry.TrimStart(static_cast<float>(s.stroke_start));
            geometry.TrimEnd(static_cast<float>(s.stroke_end));
            p.stroke.Geometry(geometry);
            p.stroke.StrokeBrush(paint_brush(gr_stroke_get_paint(s.stroke), false));
            p.stroke.StrokeThickness(static_cast<float>(st.width));
            auto cap = st.cap == GR_CAP_ROUND    ? CompositionStrokeCap::Round
                       : st.cap == GR_CAP_SQUARE ? CompositionStrokeCap::Square
                                                 : CompositionStrokeCap::Flat;
            p.stroke.StrokeStartCap(cap);
            p.stroke.StrokeEndCap(cap);
            p.stroke.StrokeDashCap(cap);
            p.stroke.StrokeLineJoin(st.join == GR_JOIN_ROUND   ? CompositionStrokeLineJoin::Round
                                    : st.join == GR_JOIN_BEVEL ? CompositionStrokeLineJoin::Bevel
                                                               : CompositionStrokeLineJoin::MiterOrBevel);
            p.stroke.StrokeMiterLimit(static_cast<float>(st.miter_limit));
            auto dashes = p.stroke.StrokeDashArray();
            dashes.Clear();
            double width = std::max(st.width, 1e-6);
            for (size_t i = 0; i < st.dash.count; ++i)
                dashes.Append(static_cast<float>(st.dash.data[i] / width));
            p.stroke.StrokeDashOffset(static_cast<float>(st.dash_phase / width));
            margin += st.width / 2 * std::max(st.miter_limit, 1.0) + st.width;
        }
        else
        {
            p.stroke.Geometry(nullptr);
            p.stroke.StrokeBrush(nullptr);
        }
        auto bounds = control_bounds(s.path);
        p.extent = bounds ? inset(*bounds, -std::ceil(margin)) : gr_rect{};
    }
    auto &p = *n.shape;
    auto e = p.extent;
    p.visual.Offset(
        {static_cast<float>(r.bounds_offset.x + e.origin.x), static_cast<float>(r.bounds_offset.y + e.origin.y), 0});
    p.visual.Size(vec(e.size.width, e.size.height));
    p.container.Offset(vec(-e.origin.x, -e.origin.y));
}

CompositionDrawingSurface Backend::acquire_image(gr_image *image)
{
    auto id = gr_image_get_id(image);
    auto it = images.find(id);
    if (it != images.end())
    {
        ++it->second.uses;
        return it->second.surface;
    }
    auto w = std::max(gr_image_get_width(image), 1u), h = std::max(gr_image_get_height(image), 1u);
    auto surface = ctx.surface(w, h);
    if (gr_image_get_width(image) && gr_image_get_height(image))
        ctx.draw(surface,
                 [&](ID2D1DeviceContext *dc, POINT at)
                 {
                     auto b = bitmap(dc, image);
                     dc->SetTransform(D2D1::Matrix3x2F::Identity());
                     D2D1_RECT_F dst{static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.x + w),
                                     static_cast<float>(at.y + h)};
                     dc->DrawBitmap(b.get(), &dst, 1, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, nullptr, nullptr);
                 });
    images.emplace(id, ImageEntry{surface, 1});
    return surface;
}

void Backend::drawing(gr_layer *id, gr_resolved const &r)
{
    auto &n = node(id);
    auto info = gr_resolved_get_draw_info(&r, scale);
    auto version = r.content.value.display_version;
    if (n.drawing && n.drawing->version == version && same_rect(n.drawing->bounds, info.bounds) &&
        n.drawing->scale == scale)
        return;
    double width = std::ceil(r.size.width * scale), height = std::ceil(r.size.height * scale);
    gr_display_list *raw = nullptr;
    if (width >= 1 && height >= 1)
        gr_drawer_provider_draw(drawers, id, version, &info, &raw);
    list_owner list(raw, gr_display_list_unref);
    if (!list || gr_display_list_is_empty(list.get()))
    {
        n.drawing.reset();
        clear_content(n);
        return;
    }
    auto w = pixel_dimension(width), h = pixel_dimension(height);
    auto surface =
        n.drawing && n.drawing->width == w && n.drawing->height == h ? n.drawing->surface : ctx.surface(w, h);
    ctx.draw(surface,
             [&](ID2D1DeviceContext *dc, POINT at)
             {
                 replay(dc, ctx.factory.get(), list.get(),
                        {scale, 0, 0, scale, r.bounds_offset.x * scale + at.x, r.bounds_offset.y * scale + at.y});
             });
    auto sprite = content_sprite(n);
    auto brush = comp().CreateSurfaceBrush(surface);
    brush.Stretch(CompositionStretch::Fill);
    sprite.Brush(brush);
    sprite.Offset({0, 0, 0});
    sprite.Size(vec(w / scale, h / scale));
    n.drawing = std::make_unique<Drawing>(Drawing{version, info.bounds, scale, w, h, surface});
}

void Backend::content(gr_layer *id, gr_resolved const &r, bool changed)
{
    auto &n = node(id);
    auto const &c = r.content;
    auto image = c.kind == GR_CONTENT_IMAGE ? c.value.image.image : nullptr;
    uint64_t wanted = image ? gr_image_get_id(image) : 0;
    if (n.image != wanted)
    {
        if (n.image)
            release_image(n.image);
        n.image = 0;
        if (image)
        {
            auto surface = acquire_image(image);
            n.image = wanted;
            auto sprite = content_sprite(n);
            auto brush = comp().CreateSurfaceBrush(surface);
            brush.Stretch(CompositionStretch::Fill);
            brush.BitmapInterpolationMode(CompositionBitmapInterpolationMode::Linear);
            sprite.Brush(brush);
        }
    }
    if (c.kind != GR_CONTENT_DRAWN)
        n.drawing.reset();
    switch (c.kind)
    {
        case GR_CONTENT_NONE:
            clear_content(n);
            break;
        case GR_CONTENT_IMAGE:
        {
            auto s = content_sprite(n);
            s.Offset(
                {static_cast<float>(c.value.image.rect.origin.x), static_cast<float>(c.value.image.rect.origin.y), 0});
            s.Size(vec(c.value.image.rect.size.width, c.value.image.rect.size.height));
            break;
        }
        case GR_CONTENT_DRAWN:
            drawing(id, r);
            break;
        case GR_CONTENT_GRADIENT:
        {
            auto s = content_sprite(n);
            if (changed)
                s.Brush(paint_brush(c.value.gradient, true));
            s.Size(vec(r.size.width, r.size.height));
            break;
        }
        case GR_CONTENT_SHAPE:
            update_shape(n, c.value.shape, r, changed);
            break;
    }
}

void Backend::update(gr_layer_update const &u)
{
    auto &n = node(u.layer);
    auto &r = u.resolved;
    auto bits = u.changed;
    if (bits & GR_DIRTY_GEOMETRY)
    {
        n.outer.TransformMatrix(matrix4(u.layer == root ? gr_affine{1, 0, 0, 1, 0, 0} : r.transform));
        n.sublayers.Offset({static_cast<float>(r.bounds_offset.x), static_cast<float>(r.bounds_offset.y), 0});
    }
    if (bits & GR_DIRTY_APPEARANCE)
    {
        n.outer.Opacity(r.props.opacity);
        n.outer.IsVisible(!r.props.hidden);
    }
    if (bits & (GR_DIRTY_GEOMETRY | GR_DIRTY_APPEARANCE))
        decoration(n, r);
    if (bits & (GR_DIRTY_GEOMETRY | GR_DIRTY_SHADOW))
        shadow(n, r);
    if (bits & GR_DIRTY_MASK)
        apply_mask(u.layer, r);
    if ((bits & (GR_DIRTY_GEOMETRY | GR_DIRTY_MASK)) && n.masking)
    {
        n.masking->layer.Size(vec(r.size.width, r.size.height));
        n.masking->surface.SourceOffset(vec(-r.bounds_offset.x, -r.bounds_offset.y));
        n.masking->surface.SourceSize(vec(r.size.width, r.size.height));
    }
    if (bits & (GR_DIRTY_GEOMETRY | GR_DIRTY_CONTENT))
        content(u.layer, r, (bits & GR_DIRTY_CONTENT) != 0);
}

void Backend::release(gr_layer *id)
{
    auto it = nodes.find(id);
    if (it == nodes.end())
        return;
    auto n = std::move(it->second);
    nodes.erase(it);
    detach(n->outer);
    if (n->image)
        release_image(n->image);
    if (auto owner = nodes.find(n->mask_owner); owner != nodes.end() && owner->second->mask == id)
    {
        owner->second->mask = nullptr;
        unmask(*owner->second);
    }
    if (auto mask = nodes.find(n->mask); mask != nodes.end())
    {
        mask->second->mask_owner = nullptr;
        detach(mask->second->outer);
    }
    if (auto parent = nodes.find(n->parent); parent != nodes.end())
        std::erase(parent->second->children, id);
    for (auto c : n->children)
        if (auto child = nodes.find(c); child != nodes.end() && child->second->parent == id)
        {
            child->second->parent = nullptr;
            detach(child->second->outer);
        }
}

void Backend::apply(gr_commit_batch const &b)
{
    gr_drawer_provider_unref(drawers);
    drawers = gr_drawer_provider_ref(b.drawers);
    bool scale_changed = scale != b.backing_scale;
    scale = b.backing_scale;
    if (scale_changed)
        scaled.Scale({static_cast<float>(scale), static_cast<float>(scale), 1});
    for (size_t i = 0; i < b.realized.count; ++i)
    {
        auto n = std::make_unique<Node>();
        n->outer = comp().CreateContainerVisual();
        n->body = comp().CreateContainerVisual();
        n->clipped = comp().CreateContainerVisual();
        n->sublayers = comp().CreateContainerVisual();
        n->clipped.Children().InsertAtTop(n->sublayers);
        n->body.Children().InsertAtTop(n->clipped);
        n->outer.Children().InsertAtTop(n->body);
        nodes.emplace(b.realized.data[i].layer, std::move(n));
    }
    if (b.root)
    {
        root = b.root;
        scaled.Children().InsertAtTop(node(root).outer);
    }
    for (size_t i = 0; i < b.updates.count; ++i)
        try
        {
            update(b.updates.data[i]);
        }
        catch (...)
        {
            error("updating layer");
        }
    for (size_t i = 0; i < b.children.count; ++i)
    {
        auto &u = b.children.data[i];
        auto &parent = node(u.parent);
        for (auto id : parent.children)
        {
            bool found = false;
            for (size_t j = 0; j < u.children.count; ++j)
                found |= u.children.data[j] == id;
            if (!found && node(id).parent == u.parent)
            {
                node(id).parent = nullptr;
                detach(node(id).outer);
            }
        }
    }
    for (size_t i = 0; i < b.children.count; ++i)
    {
        auto &u = b.children.data[i];
        for (size_t j = 0; j < u.children.count; ++j)
        {
            auto id = u.children.data[j];
            clear_mask_owner(id);
            auto &child = node(id);
            if (child.parent && child.parent != u.parent)
            {
                auto old = nodes.find(child.parent);
                if (old != nodes.end())
                    std::erase(old->second->children, id);
            }
            child.parent = u.parent;
        }
        auto &parent = node(u.parent);
        parent.children.clear();
        for (size_t j = 0; j < u.children.count; ++j)
            parent.children.push_back(u.children.data[j]);
        parent.sublayers.Children().RemoveAll();
        for (auto id : parent.children)
        {
            detach(node(id).outer);
            parent.sublayers.Children().InsertAtTop(node(id).outer);
        }
    }
    for (size_t i = 0; i < b.released.count; ++i)
        release(b.released.data[i]);
    if (scale_changed)
        for (auto &entry : nodes)
        {
            auto &n = *entry.second;
            if (n.shadow && n.shadow->valid && n.shadow->scale != scale)
                raster_shadow(n, n.shadow->shape, n.shadow->offset);
        }
}

static void request_backend(void *p)
{
    auto &b = *static_cast<Backend *>(p);
    try
    {
        schedule(b.scene.get());
    }
    catch (...)
    {
        b.error("scheduling commit");
    }
}

static void apply_backend(void *p, gr_commit_batch const *batch)
{
    auto &b = *static_cast<Backend *>(p);
    try
    {
        b.apply(*batch);
    }
    catch (...)
    {
        b.error("applying batch");
    }
}

static void destroy_backend(void *p)
{
    delete static_cast<Backend *>(p);
}

bool install(gr_scene *scene, ContainerVisual const &scaled)
{
    auto backend = std::make_unique<Backend>(scene, scaled);
    gr_backend_vtable v{request_backend, apply_backend, destroy_backend};
    if (gr_scene_set_backend(scene, &v, backend.get()) != GR_OK)
        return false;
    backend.release();
    return true;
}
} // namespace gr_win
