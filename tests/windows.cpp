#define NOMINMAX
#include <windows.h>
#include <graphite/windows.h>
#include "test.h"
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
    gr_windows_host_unref(host);
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
    gr_windows_host_unref(host);
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
    gr_windows_host_unref(host);
    CHECK(container.Children().Count() == 0);
    gr_scene_unref(scene);
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
    gr_scene_unref(scene);
}

int main(int argc, char **argv)
{
    const gr_test_case cases[] = {GR_TEST_CASE(window_hosting), GR_TEST_CASE(offscreen_alpha_and_lifetime),
                                  GR_TEST_CASE(visual_hosting), GR_TEST_CASE(invalid_arguments)};
    return gr_test_run("windows", cases, sizeof(cases) / sizeof(cases[0]), argc, argv);
}
