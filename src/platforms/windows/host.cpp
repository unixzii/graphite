#include "internal.hpp"

using namespace gr_win;

gr_windows_host::~gr_windows_host()
{
    try
    {
        if (hwnd)
        {
            if (target)
                target.Root(nullptr);
            target = nullptr;
            DestroyWindow(hwnd);
        }
        else if (visual)
        {
            if (auto parent = visual.Parent())
                parent.Children().Remove(visual);
        }
    }
    catch (...)
    {
        report("destroying host");
    }
}

static void destroy_host(void *data)
{
    delete static_cast<gr_windows_host *>(data);
}

static const gr_scene_host_vtable host_vtable = {destroy_host};

gr_windows_host *gr_windows_host_data(gr_scene_host const *host)
{
    if (!host || gr_scene_host_get_vtable(host) != &host_vtable)
        return nullptr;
    return static_cast<gr_windows_host *>(gr_scene_host_get_data(host));
}

static gr_scene_host *wrap_host(gr_scene *scene, std::unique_ptr<gr_windows_host> host)
{
    gr_scene_host *result = gr_scene_host_new(scene, &host_vtable, host.get());
    if (result)
        host.release();
    return result;
}

static void attach_visual(gr_windows_host &host, gr_scene *scene, bool backdrop)
{
    auto &ctx = context();
    auto comp = ctx.compositor;
    host.visual = comp.CreateContainerVisual();
    host.visual.RelativeSizeAdjustment({1, 1});
    host.visual.Clip(comp.CreateInsetClip());
    if (backdrop)
    {
        host.backdrop = comp.CreateSpriteVisual();
        host.backdrop.RelativeSizeAdjustment({1, 1});
        host.backdrop.Brush(comp.CreateColorBrush(Color{255, 0, 0, 0}));
        host.visual.Children().InsertAtBottom(host.backdrop);
    }
    auto scaled = comp.CreateContainerVisual();
    scaled.BorderMode(CompositionBorderMode::Soft);
    host.visual.Children().InsertAtTop(scaled);
    if (!install(scene, scaled))
        throw_hresult(E_ILLEGAL_METHOD_CALL);
}

void *gr_windows_get_compositor(void)
{
    try
    {
        return get_abi(context().compositor);
    }
    catch (...)
    {
        report("creating compositor");
        return nullptr;
    }
}

gr_scene_host *gr_windows_attach_to_hwnd(gr_scene *scene, void *parent)
{
    if (!scene || !IsWindow(static_cast<HWND>(parent)))
        return nullptr;
    try
    {
        auto host = std::make_unique<gr_windows_host>();
        auto &ctx = context();
        RECT r{};
        check_bool(GetClientRect(static_cast<HWND>(parent), &r));
        host->hwnd = create_window(scene, true, WS_EX_NOREDIRECTIONBITMAP, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0,
                                   0, r.right, r.bottom, static_cast<HWND>(parent));
        check_hresult(ctx.compositor.as<ICompositorDesktopInterop>()->CreateDesktopWindowTarget(
            host->hwnd, false,
            reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget **>(put_abi(host->target))));
        attach_visual(*host, scene, false);
        host->target.Root(host->visual);
        gr_scene_set_backing_scale(scene, window_scale(host->hwnd));
        return wrap_host(scene, std::move(host));
    }
    catch (...)
    {
        report("attaching HWND");
        return nullptr;
    }
}

gr_scene_host *gr_windows_attach_to_visual(gr_scene *scene, void *pointer)
{
    if (!scene || !pointer)
        return nullptr;
    try
    {
        ContainerVisual container{nullptr};
        copy_from_abi(container, pointer);
        auto host = std::make_unique<gr_windows_host>();
        attach_visual(*host, scene, false);
        container.Children().InsertAtTop(host->visual);
        return wrap_host(scene, std::move(host));
    }
    catch (...)
    {
        report("attaching visual");
        return nullptr;
    }
}

gr_scene_host *gr_windows_offscreen_new(gr_scene *scene, double scale)
{
    if (!scene || !std::isfinite(scale) || scale <= 0)
        return nullptr;
    auto size = gr_layer_get_bounds(gr_scene_get_root(scene)).size;
    double w = std::max(1.0, std::ceil(size.width * scale)), h = std::max(1.0, std::ceil(size.height * scale));
    if (!std::isfinite(w) || !std::isfinite(h) || w > INT_MAX || h > INT_MAX)
        return nullptr;
    try
    {
        auto host = std::make_unique<gr_windows_host>();
        auto &ctx = context();
        host->width = static_cast<uint32_t>(w);
        host->height = static_cast<uint32_t>(h);
        auto previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        try
        {
            host->hwnd =
                create_window(scene, false, WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                              WS_POPUP | WS_VISIBLE, -20000, -20000, static_cast<int>(w), static_cast<int>(h), nullptr);
        }
        catch (...)
        {
            SetThreadDpiAwarenessContext(previous);
            throw;
        }
        SetThreadDpiAwarenessContext(previous);
        check_hresult(ctx.compositor.as<ICompositorDesktopInterop>()->CreateDesktopWindowTarget(
            host->hwnd, false,
            reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget **>(put_abi(host->target))));
        attach_visual(*host, scene, true);
        host->target.Root(host->visual);
        gr_scene_set_backing_scale(scene, scale);
        return wrap_host(scene, std::move(host));
    }
    catch (...)
    {
        report("attaching offscreen");
        return nullptr;
    }
}

void *gr_windows_host_get_hwnd(gr_scene_host const *host)
{
    auto data = gr_windows_host_data(host);
    return data ? data->hwnd : nullptr;
}

void *gr_windows_host_get_visual(gr_scene_host const *host)
{
    auto data = gr_windows_host_data(host);
    return data ? get_abi(data->visual) : nullptr;
}
