#include "internal.hpp"

namespace gr_win
{
com_ptr<ID2D1Bitmap1> bitmap(ID2D1DeviceContext *dc, gr_image const *image)
{
    D2D1_BITMAP_PROPERTIES1 props{};
    props.pixelFormat = {DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
    props.dpiX = props.dpiY = 96;
    auto bytes = gr_image_get_pixels(image);
    com_ptr<ID2D1Bitmap1> result;
    check_hresult(dc->CreateBitmap({gr_image_get_width(image), gr_image_get_height(image)}, bytes.data,
                                   gr_image_get_width(image) * 4, &props, result.put()));
    return result;
}

static com_ptr<ID2D1Brush> brush(ID2D1DeviceContext *dc, gr_paint *paint, float alpha)
{
    auto p = gr_paint_get_desc(paint);
    D2D1_BRUSH_PROPERTIES props{alpha, D2D1::Matrix3x2F::Identity()};
    if (p.kind == GR_PAINT_SOLID)
    {
        com_ptr<ID2D1SolidColorBrush> b;
        auto c = d2d_color(p.value.solid);
        check_hresult(dc->CreateSolidColorBrush(&c, &props, b.put()));
        return b.as<ID2D1Brush>();
    }
    auto input = p.kind == GR_PAINT_LINEAR ? p.value.linear.stops : p.value.radial.stops;
    std::vector<D2D1_GRADIENT_STOP> stops;
    for (size_t i = 0; i < input.count; ++i)
        stops.push_back({input.data[i].offset, d2d_color(input.data[i].color)});
    com_ptr<ID2D1GradientStopCollection> collection;
    check_hresult(dc->CreateGradientStopCollection(stops.data(), static_cast<UINT32>(stops.size()), D2D1_GAMMA_2_2,
                                                   D2D1_EXTEND_MODE_CLAMP, collection.put()));
    if (p.kind == GR_PAINT_LINEAR)
    {
        com_ptr<ID2D1LinearGradientBrush> b;
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lp{point(p.value.linear.start), point(p.value.linear.end)};
        check_hresult(dc->CreateLinearGradientBrush(&lp, &props, collection.get(), b.put()));
        return b.as<ID2D1Brush>();
    }
    com_ptr<ID2D1RadialGradientBrush> b;
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES rp{point(p.value.radial.center),
                                             {0, 0},
                                             static_cast<float>(p.value.radial.radius),
                                             static_cast<float>(p.value.radial.radius)};
    check_hresult(dc->CreateRadialGradientBrush(&rp, &props, collection.get(), b.put()));
    return b.as<ID2D1Brush>();
}

static com_ptr<ID2D1PathGeometry> outline(ID2D1Factory1 *factory, gr_path *path, gr_stroke_style const &style,
                                          gr_affine transform)
{
    auto g = geometry(factory, path, GR_FILL_NONZERO);
    auto cap = style.cap == GR_CAP_ROUND    ? D2D1_CAP_STYLE_ROUND
               : style.cap == GR_CAP_SQUARE ? D2D1_CAP_STYLE_SQUARE
                                            : D2D1_CAP_STYLE_FLAT;
    auto join = style.join == GR_JOIN_ROUND   ? D2D1_LINE_JOIN_ROUND
                : style.join == GR_JOIN_BEVEL ? D2D1_LINE_JOIN_BEVEL
                                              : D2D1_LINE_JOIN_MITER_OR_BEVEL;
    double w = std::max(style.width, 1e-6);
    std::vector<float> dashes;
    for (size_t i = 0; i < style.dash.count; ++i)
        dashes.push_back(static_cast<float>(style.dash.data[i] / w));
    D2D1_STROKE_STYLE_PROPERTIES props{cap,
                                       cap,
                                       cap,
                                       join,
                                       static_cast<float>(style.miter_limit),
                                       dashes.empty() ? D2D1_DASH_STYLE_SOLID : D2D1_DASH_STYLE_CUSTOM,
                                       static_cast<float>(style.dash_phase / w)};
    com_ptr<ID2D1StrokeStyle> stroke;
    check_hresult(factory->CreateStrokeStyle(&props, dashes.data(), static_cast<UINT32>(dashes.size()), stroke.put()));
    double unit = std::max(std::sqrt(std::max(transform.a * transform.a + transform.b * transform.b,
                                              transform.c * transform.c + transform.d * transform.d)),
                           1e-6);
    com_ptr<ID2D1PathGeometry> result;
    check_hresult(factory->CreatePathGeometry(result.put()));
    com_ptr<ID2D1GeometrySink> sink;
    check_hresult(result->Open(sink.put()));
    check_hresult(g->Widen(static_cast<float>(style.width), stroke.get(), nullptr,
                           static_cast<float>(D2D1_DEFAULT_FLATTENING_TOLERANCE / unit), sink.get()));
    check_hresult(sink->Close());
    return result;
}

struct State
{
    gr_affine transform{1, 0, 0, 1, 0, 0};
    float alpha = 1;
    std::vector<bool> clips;
};

void replay(ID2D1DeviceContext *dc, ID2D1Factory1 *factory, gr_display_list const *list, gr_affine base)
{
    State state;
    std::vector<State> saved;
    std::unordered_map<uint64_t, com_ptr<ID2D1Bitmap1>> bitmaps;
    auto set_transform = [&]
    {
        auto m = matrix(then(state.transform, base));
        dc->SetTransform(m);
    };
    auto pop = [&](State const &s)
    {
        for (auto it = s.clips.rbegin(); it != s.clips.rend(); ++it)
        {
            if (*it)
                dc->PopLayer();
            else
                dc->PopAxisAlignedClip();
        }
    };
    auto push_layer = [&](ID2D1Geometry *mask)
    {
        D2D1_LAYER_PARAMETERS1 p{{-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX},
                                 mask,
                                 D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                 D2D1::Matrix3x2F::Identity(),
                                 1,
                                 nullptr,
                                 D2D1_LAYER_OPTIONS1_NONE};
        dc->PushLayer(&p, nullptr);
        state.clips.push_back(true);
    };
    set_transform();
    try
    {
        auto ops = gr_display_list_get_ops(list);
        for (size_t i = 0; i < ops.count; ++i)
        {
            auto &op = ops.data[i];
            switch (op.kind)
            {
                case GR_DRAW_SAVE:
                    saved.push_back(state);
                    state.clips.clear();
                    break;
                case GR_DRAW_RESTORE:
                    if (!saved.empty())
                    {
                        pop(state);
                        state = std::move(saved.back());
                        saved.pop_back();
                        set_transform();
                    }
                    break;
                case GR_DRAW_TRANSFORM:
                    state.transform = then(op.value.transform, state.transform);
                    set_transform();
                    break;
                case GR_DRAW_ALPHA:
                    state.alpha *= op.value.alpha;
                    break;
                case GR_DRAW_CLIP_RECT:
                    if (state.transform.b == 0 && state.transform.c == 0)
                    {
                        auto r = rect(op.value.clip_rect);
                        dc->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                        state.clips.push_back(false);
                    }
                    else
                    {
                        auto b = gr_path_builder_new();
                        gr_path_builder_add_rect(b, op.value.clip_rect);
                        path_owner path(gr_path_builder_finish(b), gr_path_unref);
                        gr_path_builder_unref(b);
                        push_layer(geometry(factory, path.get(), GR_FILL_NONZERO).get());
                    }
                    break;
                case GR_DRAW_CLIP_PATH:
                    push_layer(geometry(factory, op.value.clip_path.path, op.value.clip_path.rule).get());
                    break;
                case GR_DRAW_FILL_PATH:
                {
                    auto g = geometry(factory, op.value.fill_path.path, op.value.fill_path.rule);
                    auto b = brush(dc, op.value.fill_path.paint, state.alpha);
                    dc->FillGeometry(g.get(), b.get());
                    break;
                }
                case GR_DRAW_STROKE_PATH:
                {
                    auto g = outline(factory, op.value.stroke_path.path,
                                     gr_stroke_get_style(op.value.stroke_path.stroke), then(state.transform, base));
                    auto b = brush(dc, gr_stroke_get_paint(op.value.stroke_path.stroke), state.alpha);
                    dc->FillGeometry(g.get(), b.get());
                    break;
                }
                case GR_DRAW_IMAGE:
                {
                    auto image = op.value.image.image;
                    auto id = gr_image_get_id(image);
                    auto found = bitmaps.find(id);
                    if (found == bitmaps.end())
                        found = bitmaps.emplace(id, bitmap(dc, image)).first;
                    auto dst = rect(op.value.image.destination);
                    dc->DrawBitmap(found->second.get(), &dst, state.alpha, D2D1_INTERPOLATION_MODE_LINEAR, nullptr,
                                   nullptr);
                    break;
                }
                case GR_DRAW_TEXT_LAYOUT:
                    /* Native text drawing lands with the DirectWrite text system. */
                    break;
            }
        }
    }
    catch (...)
    {
        pop(state);
        for (auto it = saved.rbegin(); it != saved.rend(); ++it)
            pop(*it);
        throw;
    }
    pop(state);
    for (auto it = saved.rbegin(); it != saved.rend(); ++it)
        pop(*it);
}
} // namespace gr_win
