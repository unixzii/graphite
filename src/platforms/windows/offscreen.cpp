#include "internal.hpp"

namespace gr_win
{
std::vector<uint8_t> read_window(HWND hwnd, uint32_t width, uint32_t height)
{
    if (!width || !height || width > INT_MAX || height > INT_MAX ||
        static_cast<uint64_t>(width) * height > SIZE_MAX / 4)
        throw_hresult(E_INVALIDARG);
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    if (!dc)
    {
        ReleaseDC(nullptr, screen);
        throw_last_error();
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap)
    {
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        throw_last_error();
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    std::vector<uint8_t> result;
    try
    {
        check_bool(PrintWindow(hwnd, dc, 2));
        result.assign(static_cast<uint8_t *>(bits),
                      static_cast<uint8_t *>(bits) + static_cast<size_t>(width) * height * 4);
    }
    catch (...)
    {
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        throw;
    }
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return result;
}
} // namespace gr_win

static bool copy_pixels(std::vector<uint8_t> const &data, uint32_t width, uint32_t height, gr_windows_pixels *out)
{
    auto p = static_cast<uint8_t *>(std::malloc(data.size()));
    if (!p)
        std::abort();
    memcpy(p, data.data(), data.size());
    *out = {width, height, p};
    return true;
}

bool gr_windows_host_capture(gr_scene_host *scene_host, gr_windows_pixels *out)
{
    if (!out)
        return false;
    *out = {};
    gr_windows_host *host = gr_windows_host_data(scene_host);
    if (!host || !host->hwnd || !host->backdrop)
        return false;
    try
    {
        auto &ctx = gr_win::context();
        auto capture = [&](uint8_t gray)
        {
            host->backdrop.Brush(ctx.compositor.CreateColorBrush({255, gray, gray, gray}));
            ctx.wait();
            return gr_win::read_window(host->hwnd, host->width, host->height);
        };
        auto black = capture(0), white = capture(255);
        std::vector<uint8_t> data(black.size());
        for (size_t i = 0; i < data.size(); i += 4)
        {
            unsigned diff = 0;
            for (unsigned j = 0; j < 3; ++j)
                diff += white[i + j] > black[i + j] ? white[i + j] - black[i + j] : 0;
            uint8_t alpha = static_cast<uint8_t>(255 - std::min((diff + 1) / 3, 255u));
            data[i] = std::min(black[i + 2], alpha);
            data[i + 1] = std::min(black[i + 1], alpha);
            data[i + 2] = std::min(black[i], alpha);
            data[i + 3] = alpha;
        }
        return copy_pixels(data, host->width, host->height, out);
    }
    catch (...)
    {
        gr_win::report("capturing offscreen");
        return false;
    }
}

bool gr_windows_capture_window(void *pointer, gr_windows_pixels *out)
{
    if (!out)
        return false;
    *out = {};
    HWND hwnd = static_cast<HWND>(pointer);
    if (!IsWindow(hwnd))
        return false;
    try
    {
        RECT r{};
        winrt::check_bool(GetWindowRect(hwnd, &r));
        uint32_t w = static_cast<uint32_t>(std::max(1L, r.right - r.left)),
                 h = static_cast<uint32_t>(std::max(1L, r.bottom - r.top));
        gr_win::context().wait();
        auto data = gr_win::read_window(hwnd, w, h);
        for (size_t i = 0; i < data.size(); i += 4)
        {
            std::swap(data[i], data[i + 2]);
            data[i + 3] = 255;
        }
        return copy_pixels(data, w, h, out);
    }
    catch (...)
    {
        gr_win::report("capturing window");
        return false;
    }
}

void gr_windows_pixels_clear(gr_windows_pixels *pixels)
{
    if (pixels)
    {
        std::free(pixels->data);
        *pixels = {};
    }
}
