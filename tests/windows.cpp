#define NOMINMAX

#include "test.h"

#include <graphite/backend.h>
#include <graphite/windows.h>

#include <windows.h>
#include <chrono>
#include <thread>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Composition.h>

static void pump()
{
    MSG m{};
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

static void clean(gr_scene *scene)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (gr_scene_needs_commit(scene))
    {
        CHECK(std::chrono::steady_clock::now() < end);
        pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

static gr_layer *square(gr_scene *scene)
{
    auto l = gr_layer_new(scene, GR_LAYER_CONTAINER);
    OK(gr_layer_set_frame(l, {{0, 0}, {10, 10}}));
    gr_color red{1, 0, 0, 1};
    OK(gr_layer_set_background(l, &red));
    OK(gr_layer_add_child(gr_scene_get_root(scene), l));
    return l;
}

static void pixel(gr_windows_pixels const &p, unsigned x, unsigned y, bool red)
{
    CHECK(x < p.width && y < p.height);
    auto v = p.data + (static_cast<size_t>(y) * p.width + x) * 4;
    if (red)
        CHECK(v[0] == 255 && v[1] == 0 && v[2] == 0 && v[3] == 255);
    else
        CHECK(v[0] == 0 && v[1] == 0 && v[2] == 0);
}

static void window_hosting()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"GraphiteCWindowTest";
    RegisterClassW(&wc);
    HWND parent = CreateWindowExW(0, wc.lpszClassName, L"graphite window test", WS_POPUP | WS_VISIBLE, 40, 40, 300, 200,
                                  nullptr, nullptr, wc.hInstance, nullptr);
    CHECK(parent);
    auto scene = gr_scene_new({120, 80});
    auto l = square(scene);
    auto host = gr_windows_attach_to_hwnd(scene, parent);
    CHECK(host);
    HWND child = static_cast<HWND>(gr_windows_host_get_hwnd(host));
    RECT client{};
    CHECK(GetClientRect(child, &client));
    CHECK(client.right == 300 && client.bottom == 200);
    double scale = GetDpiForWindow(child) / 96.0;
    CHECK_NEAR(gr_scene_get_backing_scale(scene), scale);
    clean(scene);
    gr_windows_pixels image{};
    CHECK(gr_windows_capture_window(child, &image));
    pixel(image, static_cast<unsigned>(5 * scale), static_cast<unsigned>(5 * scale), true);
    pixel(image, static_cast<unsigned>(5 * scale), static_cast<unsigned>(15 * scale), false);
    gr_windows_pixels_clear(&image);
    auto settle = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < settle)
    {
        pump();
        Sleep(5);
    }
    OK(gr_layer_set_position(l, {65, 45}));
    clean(scene);
    CHECK(gr_windows_capture_window(child, &image));
    pixel(image, static_cast<unsigned>(65 * scale), static_cast<unsigned>(45 * scale), true);
    pixel(image, 5, 5, false);
    gr_windows_pixels_clear(&image);
    CHECK(MoveWindow(child, 0, 0, 160, 100, TRUE));
    CHECK(GetClientRect(child, &client));
    CHECK(client.right == 160 && client.bottom == 100);
    gr_scene_host_unref(host);
    CHECK(!IsWindow(child));
    gr_layer_unref(l);
    gr_scene_unref(scene);
    DestroyWindow(parent);
    pump();
}

static void offscreen_alpha_and_lifetime()
{
    auto scene = gr_scene_new({100, 100});
    auto l = square(scene);
    OK(gr_layer_set_opacity(l, 0.5f));
    auto host = gr_windows_offscreen_new(scene, 1.5);
    CHECK(host);
    CHECK(!gr_windows_offscreen_new(scene, 1));
    OK(gr_scene_commit(scene));
    gr_windows_pixels image{};
    CHECK(gr_windows_host_capture(host, &image));
    CHECK(image.width == 150 && image.height == 150);
    auto p = image.data + (5 * 150 + 5) * 4;
    CHECK(p[0] >= 126 && p[0] <= 128 && p[3] >= 126 && p[3] <= 128);
    CHECK(image.data[(90 * 150 + 90) * 4 + 3] == 0);
    gr_windows_pixels_clear(&image);
    gr_scene_unref(scene);
    CHECK(!gr_layer_is_alive(l));
    CHECK(gr_windows_host_capture(host, &image));
    CHECK(image.data[(5 * 150 + 5) * 4 + 3] == 0);
    gr_windows_pixels_clear(&image);
    gr_layer_unref(l);
    gr_scene_host_unref(host);
    pump();
}

static void visual_hosting()
{
    winrt::Windows::UI::Composition::Compositor compositor{nullptr};
    winrt::copy_from_abi(compositor, gr_windows_get_compositor());
    auto container = compositor.CreateContainerVisual();
    auto scene = gr_scene_new({40, 40});
    auto host = gr_windows_attach_to_visual(scene, winrt::get_abi(container));
    CHECK(host && !gr_windows_host_get_hwnd(host));
    CHECK(container.Children().Count() == 1);
    OK(gr_scene_commit(scene));
    gr_scene_host_unref(host);
    CHECK(container.Children().Count() == 0);
    gr_scene_unref(scene);
    pump();
}

struct EventState
{
    gr_scene *scene = nullptr;
    gr_layer *layer = nullptr;
    unsigned frames = 0, missed = 0, viewports = 0, focus_ins = 0, focus_outs = 0;
    gr_event last_frame{};
    gr_event last_viewport{};
};

static bool on_event(void *userdata, gr_event const *event)
{
    auto &s = *static_cast<EventState *>(userdata);
    switch (event->type)
    {
        case GR_EVENT_FRAME:
            ++s.frames;
            s.last_frame = *event;
            /* The host commits right after delivering each frame. */
            if (gr_scene_needs_commit(s.scene))
                ++s.missed;
            OK(gr_layer_set_position(s.layer, {5.0 + s.frames, 5}));
            return true;
        case GR_EVENT_VIEWPORT:
            ++s.viewports;
            s.last_viewport = *event;
            return true;
        case GR_EVENT_FOCUS_IN:
            ++s.focus_ins;
            return true;
        case GR_EVENT_FOCUS_OUT:
            ++s.focus_outs;
            return true;
        default:
            return false;
    }
}

static void wait_frames(EventState &s, unsigned frames)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (s.frames < frames)
    {
        CHECK(std::chrono::steady_clock::now() < end);
        pump();
        Sleep(1);
    }
}

static void window_events()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"GraphiteCEventTest";
    RegisterClassW(&wc);
    HWND parent = CreateWindowExW(0, wc.lpszClassName, L"graphite event test", WS_POPUP | WS_VISIBLE, 40, 40, 300, 200,
                                  nullptr, nullptr, wc.hInstance, nullptr);
    CHECK(parent);
    EventState s;
    s.scene = gr_scene_new({120, 80});
    s.layer = square(s.scene);

    auto offscreen = gr_windows_offscreen_new(s.scene, 1);
    CHECK(offscreen && !gr_scene_host_is_interactive(offscreen));
    gr_scene_host_unref(offscreen);
    gr_scene_unref(s.scene);
    gr_layer_unref(s.layer);
    s.scene = gr_scene_new({120, 80});
    s.layer = square(s.scene);

    auto host = gr_windows_attach_to_hwnd(s.scene, parent);
    CHECK(host && gr_scene_host_is_interactive(host));
    HWND child = static_cast<HWND>(gr_windows_host_get_hwnd(host));
    double scale = GetDpiForWindow(child) / 96.0;
    gr_viewport_event viewport{};
    CHECK(gr_scene_host_get_viewport(host, &viewport)); /* Recorded at attachment. */
    CHECK_NEAR(viewport.size.width, 300 / scale);
    CHECK_NEAR(viewport.size.height, 200 / scale);
    CHECK_NEAR(viewport.scale, scale);

    gr_event_handler handler{&s, on_event, nullptr};
    OK(gr_scene_host_set_event_handler(host, &handler));
    CHECK(MoveWindow(child, 0, 0, 160, 100, TRUE));
    CHECK(s.viewports == 1);
    CHECK_NEAR(s.last_viewport.value.viewport.size.width, 160 / scale);

    /* A frame per request, committed before the next, in the frame clock's time domain. */
    for (unsigned i = 1; i <= 10; ++i)
    {
        OK(gr_scene_host_request_frame(host));
        wait_frames(s, i);
    }
    auto frame = s.last_frame.value.frame;
    CHECK(s.last_frame.time <= gr_scene_host_get_time(host));
    CHECK(frame.interval > 0 && frame.interval < 0.5 && frame.target_time > s.last_frame.time);
    auto settle = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() < settle)
    {
        pump();
        Sleep(5);
    }
    CHECK(s.frames == 10 && s.missed == 0);

    /* Focus follows the host window's keyboard focus. */
    OK(gr_scene_host_request_focus(host));
    pump();
    CHECK(GetFocus() == child && gr_scene_host_has_focus(host) && s.focus_ins == 1);
    SetFocus(parent);
    pump();
    CHECK(!gr_scene_host_has_focus(host) && s.focus_outs == 1);
    OK(gr_scene_host_request_focus(host));
    CHECK(gr_scene_host_has_focus(host));

    /* Release destroys the window without reporting focus loss to a dead host, and stops pending frames. */
    OK(gr_scene_host_request_frame(host));
    gr_scene_host_unref(host);
    CHECK(!IsWindow(child) && s.focus_outs == 1);
    settle = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() < settle)
    {
        pump();
        Sleep(5);
    }
    CHECK(s.frames == 10);
    gr_layer_unref(s.layer);
    gr_scene_unref(s.scene);
    DestroyWindow(parent);
    pump();
}

static void invalid_arguments()
{
    gr_windows_pixels p{};
    CHECK(!gr_windows_capture_window(nullptr, &p));
    CHECK(!gr_windows_host_capture(nullptr, &p));
    CHECK(!p.data);
    gr_windows_pixels_clear(nullptr);
    auto scene = gr_scene_new({1, 1});
    CHECK(!gr_windows_attach_to_hwnd(scene, nullptr));
    CHECK(!gr_windows_offscreen_new(scene, 0));
    CHECK(!gr_windows_offscreen_new(scene, -1));
    static const gr_scene_host_vtable foreign_vtable = {.destroy = [](void *) {}};
    auto foreign = gr_scene_host_new(scene, &foreign_vtable, nullptr);
    CHECK(!gr_windows_host_get_hwnd(foreign) && !gr_windows_host_get_visual(foreign));
    CHECK(!gr_windows_host_capture(foreign, &p) && !p.data);
    gr_scene_host_unref(foreign);
    gr_scene_unref(scene);
}

int main(int argc, char **argv)
{
    const gr_test_case cases[] = {GR_TEST_CASE(window_hosting), GR_TEST_CASE(offscreen_alpha_and_lifetime),
                                  GR_TEST_CASE(visual_hosting), GR_TEST_CASE(invalid_arguments),
                                  GR_TEST_CASE(window_events)};
    return gr_test_run("windows", cases, sizeof(cases) / sizeof(cases[0]), argc, argv);
}
