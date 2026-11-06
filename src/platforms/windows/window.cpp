#include "internal.hpp"
#include <mutex>

namespace gr_win
{
struct WindowData
{
    gr_scene_link *scene;
    bool track;
    bool *creation_claimed;

    ~WindowData()
    {
        gr_scene_link_unref(scene);
    }
};

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    try
    {
        switch (message)
        {
            case WM_NCCREATE:
            {
                auto create = reinterpret_cast<CREATESTRUCTW *>(lp);
                auto data = static_cast<WindowData *>(create->lpCreateParams);
                *data->creation_claimed = true;
                data->creation_claimed = nullptr;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
                break;
            }
            case WM_NCDESTROY:
            {
                auto data = reinterpret_cast<WindowData *>(SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0));
                delete data;
                break;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT:
                ValidateRect(hwnd, nullptr);
                return 0;
            case WM_NCHITTEST:
                return HTTRANSPARENT;
            case WM_DPICHANGED:
            case WM_DPICHANGED_AFTERPARENT:
            {
                auto data = reinterpret_cast<WindowData *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                if (data && data->track)
                {
                    gr_scene *s = gr_scene_link_lock(data->scene);
                    if (s)
                    {
                        gr_scene_set_backing_scale(s, window_scale(hwnd));
                        gr_scene_unref(s);
                    }
                }
                break;
            }
        }
    }
    catch (...)
    {
        report("window callback");
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

double window_scale(HWND hwnd)
{
    UINT dpi = GetDpiForWindow(hwnd);
    return dpi ? dpi / 96.0 : 1;
}

HWND create_window(gr_scene *scene, bool track, DWORD ex, DWORD style, int x, int y, int w, int h, HWND parent)
{
    static std::once_flag once;
    std::call_once(once,
                   []
                   {
                       WNDCLASSW wc{};
                       wc.style = CS_HREDRAW | CS_VREDRAW;
                       wc.lpfnWndProc = window_proc;
                       wc.hInstance = GetModuleHandleW(nullptr);
                       wc.lpszClassName = L"GraphiteCSceneHost";
                       if (!RegisterClassW(&wc))
                           throw_last_error();
                   });
    bool claimed = false;
    auto data = new WindowData{gr_scene_get_link(scene), track, &claimed};
    HWND hwnd = CreateWindowExW(ex, L"GraphiteCSceneHost", L"graphite", style, x, y, w, h, parent, nullptr,
                                GetModuleHandleW(nullptr), data);
    if (!hwnd)
    {
        if (!claimed)
            delete data;
        throw_last_error();
    }
    return hwnd;
}
} // namespace gr_win
