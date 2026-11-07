#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "demo.h"

#include <graphite/windows.h>

#include <windows.h>
#include <atomic>
#include <chrono>
#include <dcomp.h>
#include <stdexcept>
#include <thread>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>

namespace
{
constexpr UINT frame_message = WM_APP + 1;

struct WindowState
{
    demo *content = nullptr;
    gr_windows_host *host = nullptr;
    std::atomic<bool> frame_pending{false};
    unsigned frames = 0, fps_ticks = 0, unflushed[60]{}, missed = 0;
    bool selftest = false, fps = false;
    int status = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now(), report = start;
};

void update_frame(WindowState &s)
{
    if (s.selftest && s.frames >= 60)
        return;
    ++s.frames;
    if (s.selftest && s.frames > 1 && gr_scene_needs_commit(s.content->scene))
        s.unflushed[s.missed++] = s.frames;
    auto now = std::chrono::steady_clock::now();
    demo_update(s.content, std::chrono::duration<double>(now - s.start).count());
    if (s.fps)
    {
        ++s.fps_ticks;
        double elapsed = std::chrono::duration<double>(now - s.report).count();
        if (elapsed >= 2)
        {
            printf("fps: %.1f\n", s.fps_ticks / elapsed);
            fflush(stdout);
            s.report = now;
            s.fps_ticks = 0;
        }
    }
    if (s.selftest && s.frames == 60)
    {
        printf("selftest: frames=%u uncommitted=[", s.frames);
        for (unsigned i = 0; i < s.missed; ++i)
            printf("%s%u", i ? ", " : "", s.unflushed[i]);
        printf("] backing_scale=%g\n", gr_scene_get_backing_scale(s.content->scene));
        fflush(stdout);
        s.status = s.missed ? 1 : 0;
        PostQuitMessage(s.status);
    }
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    auto state = reinterpret_cast<WindowState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        state = static_cast<WindowState *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    switch (message)
    {
        case frame_message:
            if (state)
            {
                update_frame(*state);
                state->frame_pending = false;
            }
            return 0;
        case WM_SIZE:
            if (state && state->host)
                MoveWindow(static_cast<HWND>(gr_windows_host_get_hwnd(state->host)), 0, 0, LOWORD(lp), HIWORD(lp),
                           TRUE);
            return 0;
        case WM_DPICHANGED:
        {
            auto r = reinterpret_cast<RECT *>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

class DisplayClock
{
    HANDLE stop_event;
    std::thread worker;

    void run(HWND window, WindowState &state) noexcept
    {
        for (;;)
        {
            DWORD result = DCompositionWaitForCompositorClock(1, &stop_event, 100);
            if (WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0)
                break;
            if (result == WAIT_OBJECT_0 + 1 || result == WAIT_OBJECT_0)
            {
                if (!state.frame_pending.exchange(true) && !PostMessageW(window, frame_message, 0, 0))
                    state.frame_pending = false;
            }
            else if (WaitForSingleObject(stop_event, 50) == WAIT_OBJECT_0)
                break;
        }
    }

  public:
    DisplayClock(HWND window, WindowState &state) : stop_event(CreateEventW(nullptr, TRUE, FALSE, nullptr))
    {
        if (!stop_event)
            throw std::runtime_error("cannot create display clock stop event");
        try
        {
            worker = std::thread([this, window, &state] { run(window, state); });
        }
        catch (...)
        {
            CloseHandle(stop_event);
            throw;
        }
    }

    ~DisplayClock()
    {
        SetEvent(stop_event);
        worker.join();
        CloseHandle(stop_event);
    }
};

bool save_png(const char *path, gr_windows_pixels const &pixels)
{
    using Microsoft::WRL::ComPtr;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
    if (!count)
        return false;
    std::vector<wchar_t> wide(static_cast<size_t>(count));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide.data(), count);
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr))
        return false;
    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(wide.data(), GENERIC_WRITE)))
        return false;
    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)))
        return false;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(pixels.width, pixels.height)))
        return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA)
        return false;
    std::vector<uint8_t> bgra(static_cast<size_t>(pixels.width) * pixels.height * 4);
    for (size_t i = 0; i < bgra.size(); i += 4)
    {
        bgra[i] = pixels.data[i + 2];
        bgra[i + 1] = pixels.data[i + 1];
        bgra[i + 2] = pixels.data[i];
        bgra[i + 3] = pixels.data[i + 3];
    }
    return SUCCEEDED(
               frame->WritePixels(pixels.height, pixels.width * 4, static_cast<UINT>(bgra.size()), bgra.data())) &&
           SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}
} // namespace

gr_text_system *demo_platform_text_system(void)
{
    return nullptr; // DirectWrite text system not implemented yet.
}

int demo_platform_run(demo *content, bool selftest, bool fps)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WindowState state;
    state.content = content;
    state.selftest = selftest;
    state.fps = fps;
    WNDCLASSW wc{};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"GraphiteCDemo";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    auto size = demo_size(content->kind);
    UINT dpi = GetDpiForSystem();
    double scale = dpi / 96.0;
    RECT rect{0, 0, static_cast<LONG>(std::round(size.width * scale)),
              static_cast<LONG>(std::round(size.height * scale))};
    AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    HWND window = CreateWindowExW(
        0, wc.lpszClassName, L"graphite demo (Windows.UI.Composition)", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
        CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, wc.hInstance, &state);
    if (!window)
        return 1;
    state.host = gr_windows_attach_to_hwnd(content->scene, window);
    if (!state.host)
    {
        DestroyWindow(window);
        return 1;
    }
    state.start = state.report = std::chrono::steady_clock::now();
    try
    {
        DisplayClock clock(window, state);
        MSG message{};
        BOOL result;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (result < 0)
            state.status = 1;
    }
    catch (std::exception const &error)
    {
        fprintf(stderr, "display clock: %s\n", error.what());
        state.status = 1;
    }
    gr_windows_host_unref(state.host);
    state.host = nullptr;
    if (IsWindow(window))
        DestroyWindow(window);
    return state.status;
}

bool demo_platform_snapshot(demo *content, const char *path)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    gr_windows_host *host = gr_windows_offscreen_new(content->scene, 1);
    if (!host)
        return false;
    gr_windows_pixels pixels{};
    bool ok = gr_scene_commit(content->scene) == GR_OK && gr_windows_host_capture(host, &pixels);
    if (ok)
    {
        try
        {
            ok = save_png(path, pixels);
        }
        catch (...)
        {
            ok = false;
        }
    }
    gr_windows_pixels_clear(&pixels);
    gr_windows_host_unref(host);
    if (!ok)
        fprintf(stderr, "cannot render or write %s\n", path);
    return ok;
}
