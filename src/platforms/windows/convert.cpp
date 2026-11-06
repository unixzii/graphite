#include "internal.hpp"

namespace gr_win
{
Color color(gr_color c)
{
    auto q = [](float v) { return static_cast<uint8_t>(std::round(std::clamp(v, 0.0f, 1.0f) * 255)); };
    return {q(c.a), q(c.r), q(c.g), q(c.b)};
}

D2D1_COLOR_F d2d_color(gr_color c)
{
    return {c.r, c.g, c.b, c.a};
}

float2 vec(double x, double y)
{
    return {static_cast<float>(x), static_cast<float>(y)};
}

D2D1_POINT_2F point(gr_point p)
{
    return {static_cast<float>(p.x), static_cast<float>(p.y)};
}

D2D1_RECT_F rect(gr_rect r)
{
    return {static_cast<float>(r.origin.x), static_cast<float>(r.origin.y),
            static_cast<float>(r.origin.x + r.size.width), static_cast<float>(r.origin.y + r.size.height)};
}

D2D1_MATRIX_3X2_F matrix(gr_affine m)
{
    return {static_cast<float>(m.a), static_cast<float>(m.b),  static_cast<float>(m.c),
            static_cast<float>(m.d), static_cast<float>(m.tx), static_cast<float>(m.ty)};
}

float4x4 matrix4(gr_affine m)
{
    return {static_cast<float>(m.a),
            static_cast<float>(m.b),
            0,
            0,
            static_cast<float>(m.c),
            static_cast<float>(m.d),
            0,
            0,
            0,
            0,
            1,
            0,
            static_cast<float>(m.tx),
            static_cast<float>(m.ty),
            0,
            1};
}

gr_affine then(gr_affine a, gr_affine b)
{
    return {b.a * a.a + b.c * a.b, b.b * a.a + b.d * a.b,          b.a * a.c + b.c * a.d,
            b.b * a.c + b.d * a.d, b.a * a.tx + b.c * a.ty + b.tx, b.b * a.tx + b.d * a.ty + b.ty};
}

gr_rect inset(gr_rect r, double d)
{
    return {{r.origin.x + d, r.origin.y + d},
            {std::max(0.0, r.size.width - 2 * d), std::max(0.0, r.size.height - 2 * d)}};
}

std::optional<gr_rect> control_bounds(gr_path const *path)
{
    double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    auto elements = gr_path_get_elements(path);
    for (size_t i = 0; i < elements.count; ++i)
    {
        auto &e = elements.data[i];
        unsigned n = e.verb == GR_PATH_CLOSE ? 0 : e.verb == GR_PATH_CUBIC ? 3 : e.verb == GR_PATH_QUAD ? 2 : 1;
        for (unsigned j = 0; j < n; ++j)
        {
            x0 = std::min(x0, e.p[j].x);
            x1 = std::max(x1, e.p[j].x);
            y0 = std::min(y0, e.p[j].y);
            y1 = std::max(y1, e.p[j].y);
        }
    }
    if (x0 > x1 || y0 > y1)
        return {};
    return gr_rect{{x0, y0}, {x1 - x0, y1 - y0}};
}

com_ptr<ID2D1PathGeometry> geometry(ID2D1Factory1 *factory, gr_path const *path, gr_fill_rule rule)
{
    com_ptr<ID2D1PathGeometry> result;
    check_hresult(factory->CreatePathGeometry(result.put()));
    com_ptr<ID2D1GeometrySink> sink;
    check_hresult(result->Open(sink.put()));
    sink->SetFillMode(rule == GR_FILL_EVEN_ODD ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING);
    bool open = false;
    gr_point start{}, current{};
    auto begin = [&]
    {
        if (!open)
        {
            sink->BeginFigure(point(current), D2D1_FIGURE_BEGIN_FILLED);
            open = true;
        }
    };
    auto elements = gr_path_get_elements(path);
    for (size_t i = 0; i < elements.count; ++i)
    {
        auto &e = elements.data[i];
        switch (e.verb)
        {
            case GR_PATH_MOVE:
                if (open)
                {
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    open = false;
                }
                start = current = e.p[0];
                break;
            case GR_PATH_LINE:
                begin();
                sink->AddLine(point(e.p[0]));
                current = e.p[0];
                break;
            case GR_PATH_QUAD:
            {
                begin();
                D2D1_QUADRATIC_BEZIER_SEGMENT b{point(e.p[0]), point(e.p[1])};
                sink->AddQuadraticBezier(b);
                current = e.p[1];
                break;
            }
            case GR_PATH_CUBIC:
            {
                begin();
                D2D1_BEZIER_SEGMENT b{point(e.p[0]), point(e.p[1]), point(e.p[2])};
                sink->AddBezier(b);
                current = e.p[2];
                break;
            }
            case GR_PATH_CLOSE:
                if (open)
                {
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    open = false;
                }
                current = start;
                break;
        }
    }
    if (open)
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    check_hresult(sink->Close());
    return result;
}

struct GeometrySource
    : implements<GeometrySource, winrt::Windows::Graphics::IGeometrySource2D, IGeometrySource2DInterop>
{
    com_ptr<ID2D1Geometry> value;

    explicit GeometrySource(com_ptr<ID2D1Geometry> geometry) : value(std::move(geometry))
    {
    }

    HRESULT __stdcall GetGeometry(ID2D1Geometry **out) noexcept override
    {
        value.copy_to(out);
        return S_OK;
    }

    HRESULT __stdcall TryGetGeometryUsingFactory(ID2D1Factory *, ID2D1Geometry **out) noexcept override
    {
        *out = nullptr;
        return E_NOTIMPL;
    }
};

CompositionPath composition_path(ID2D1Factory1 *factory, gr_path const *path, gr_fill_rule rule)
{
    return CompositionPath(make<GeometrySource>(geometry(factory, path, rule).as<ID2D1Geometry>()));
}
} // namespace gr_win
