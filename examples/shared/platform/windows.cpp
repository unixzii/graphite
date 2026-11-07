#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "platform.h"

#include <graphite/windows.h>

#include <windows.h>
#include <cmath>
#include <string>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>

namespace
{
struct WindowState
{
    example_frames frames{};
    gr_scene_host *host = nullptr;
    int status = 0;
};

void finish_selftest(example_frames *frames)
{
    auto state = static_cast<WindowState *>(frames->platform);
    state->status = example_frames_report(frames, nullptr, true);
    PostQuitMessage(state->status);
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

gr_text_system *example_platform_text_system(void)
{
    return gr_windows_text_system_new();
}

int example_platform_run(example *content, bool selftest, bool fps)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WindowState state;
    WNDCLASSW wc{};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"GraphiteExample";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    auto size = content->desc->size;
    UINT dpi = GetDpiForSystem();
    double scale = dpi / 96.0;
    RECT rect{0, 0, static_cast<LONG>(std::round(size.width * scale)),
              static_cast<LONG>(std::round(size.height * scale))};
    AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    std::wstring title = L"graphite ";
    for (const char *c = content->desc->name; *c; ++c) /* Example names are ASCII. */
        title += static_cast<wchar_t>(*c);
    title += L" (Windows.UI.Composition)";
    HWND window = CreateWindowExW(0, wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                                  CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                                  wc.hInstance, &state);
    if (!window)
        return 1;
    state.host = gr_windows_attach_to_hwnd(content->scene, window);
    if (!state.host)
    {
        DestroyWindow(window);
        return 1;
    }
    /* The host commits right after delivering each frame. */
    state.frames.content = content;
    state.frames.host = state.host;
    state.frames.selftest = selftest;
    state.frames.fps = fps;
    state.frames.strict = true;
    state.frames.finished = finish_selftest;
    state.frames.platform = &state;
    if (example_frames_start(&state.frames))
    {
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
    else
        state.status = 1;
    /* Releasing the host destroys the frame handler, which borrows state. */
    content->host = nullptr;
    gr_scene_host_unref(state.host);
    state.host = nullptr;
    if (IsWindow(window))
        DestroyWindow(window);
    return state.status;
}

bool example_platform_snapshot(example *content, const char *path)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    gr_scene_host *host = gr_windows_offscreen_new(content->scene, 1);
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
    gr_scene_host_unref(host);
    if (!ok)
        fprintf(stderr, "cannot render or write %s\n", path);
    return ok;
}
