#include "internal.hpp"

#include <dcomp.h>
#include <mutex>

namespace gr_win
{
constexpr UINT frame_message = WM_APP + 1;

struct WindowData
{
    gr_scene_link *scene;
    bool track;
    bool *creation_claimed;
    gr_scene_host *host = nullptr; /* Borrowed; cleared before the host is destroyed. */

    ~WindowData()
    {
        gr_scene_link_unref(scene);
    }
};

static double qpc_seconds(int64_t ticks)
{
    static const double frequency = []
    {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    return static_cast<double>(ticks) / frequency;
}

double seconds_now()
{
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return qpc_seconds(now.QuadPart);
}

/* The compositor's frame period, or 60 Hz when it reports none. */
static double frame_period()
{
    COMPOSITION_FRAME_ID id = 0;
    COMPOSITION_FRAME_STATS stats{};
    if (SUCCEEDED(DCompositionGetFrameId(COMPOSITION_FRAME_ID_CONFIRMED, &id)) &&
        SUCCEEDED(DCompositionGetStatistics(id, &stats, 0, nullptr, nullptr)) && stats.framePeriod > 0)
        return qpc_seconds(static_cast<int64_t>(stats.framePeriod));
    return 1.0 / 60;
}

static WindowData *window_data(HWND hwnd)
{
    return reinterpret_cast<WindowData *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

/* Delivers event to the window's host. The handler may release the host, which destroys this window; callers must
 * not touch the window or its data afterwards. */
static void dispatch(WindowData *data, gr_event const &event)
{
    if (data && data->host)
        gr_scene_host_dispatch_event(data->host, &event);
}

void set_window_host(HWND hwnd, gr_scene_host *host)
{
    if (auto data = window_data(hwnd))
        data->host = host;
}

void report_viewport(HWND hwnd)
{
    RECT r{};
    GetClientRect(hwnd, &r);
    double scale = window_scale(hwnd);
    gr_event event{};
    event.type = GR_EVENT_VIEWPORT;
    event.time = seconds_now();
    event.value.viewport = {{r.right / scale, r.bottom / scale}, scale};
    dispatch(window_data(hwnd), event);
}

/* Delivers a frame, then commits at once so its changes make the compositor frame that follows. */
static void deliver_frame(WindowData *data)
{
    gr_scene_host *host = data ? data->host : nullptr;
    auto win = gr_windows_host_data(host);
    if (!win || !win->clock)
        return;
    double time = win->clock->take_tick(), interval = frame_period();
    gr_event event{};
    event.type = GR_EVENT_FRAME;
    event.time = time;
    event.value.frame = {time + interval, interval};
    gr_scene_host_ref(host);
    gr_scene_host_dispatch_event(host, &event);
    if (gr_scene *scene = gr_scene_host_get_scene(host))
    {
        if (gr_scene_needs_commit(scene))
            gr_scene_commit(scene);
        gr_scene_unref(scene);
    }
    gr_scene_host_unref(host);
}

FrameClock::FrameClock(HWND window) : hwnd(window)
{
    stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    requested = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!stop || !requested)
    {
        if (stop)
            CloseHandle(stop);
        if (requested)
            CloseHandle(requested);
        throw_last_error();
    }
    try
    {
        worker = std::thread([this] { run(); });
    }
    catch (...)
    {
        CloseHandle(stop);
        CloseHandle(requested);
        throw;
    }
}

FrameClock::~FrameClock()
{
    SetEvent(stop);
    worker.join();
    CloseHandle(stop);
    CloseHandle(requested);
}

void FrameClock::request()
{
    SetEvent(requested);
}

double FrameClock::take_tick()
{
    double time = qpc_seconds(tick.load());
    pending = false;
    return time;
}

void FrameClock::run() noexcept
{
    HANDLE waits[2] = {stop, requested};
    while (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
    {
        DWORD result = DCompositionWaitForCompositorClock(1, &stop, INFINITE);
        if (result == WAIT_OBJECT_0)
            return;
        /* Without a compositor clock, fall back to a 60 Hz timer. */
        if (result != WAIT_OBJECT_0 + 1 && WaitForSingleObject(stop, 16) == WAIT_OBJECT_0)
            return;
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        tick = now.QuadPart;
        if (!pending.exchange(true) && !PostMessageW(hwnd, frame_message, 0, 0))
            pending = false;
    }
}

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
                auto data = window_data(hwnd);
                if (data && data->track)
                {
                    gr_scene *s = gr_scene_link_lock(data->scene);
                    if (s)
                    {
                        gr_scene_set_backing_scale(s, window_scale(hwnd));
                        gr_scene_unref(s);
                    }
                    report_viewport(hwnd);
                    return 0;
                }
                break;
            }
            /* Event messages return right after dispatch: a handler may destroy this window. */
            case WM_SIZE:
                if (auto data = window_data(hwnd); data && data->host)
                {
                    report_viewport(hwnd);
                    return 0;
                }
                break;
            case WM_SETFOCUS:
            case WM_KILLFOCUS:
            {
                gr_event event{};
                event.type = message == WM_SETFOCUS ? GR_EVENT_FOCUS_IN : GR_EVENT_FOCUS_OUT;
                event.time = seconds_now();
                dispatch(window_data(hwnd), event);
                return 0;
            }
            case frame_message:
                deliver_frame(window_data(hwnd));
                return 0;
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
