#define NOMINMAX

#include "test.h"

#include <graphite/backend.h>
#include <graphite/windows.h>

#include <windows.h>
#include <chrono>
#include <cmath>
#include <imm.h>
#include <string>
#include <thread>
#include <vector>
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

struct InputLog
{
    std::vector<gr_event> events;
    bool handled = true;

    gr_event const *last(gr_event_type type) const
    {
        for (auto it = events.rbegin(); it != events.rend(); ++it)
            if (it->type == type)
                return &*it;
        return nullptr;
    }
};

static bool log_input(void *userdata, gr_event const *event)
{
    auto &log = *static_cast<InputLog *>(userdata);
    log.events.push_back(*event);
    return log.handled;
}

static LPARAM key_lparam(unsigned scan, bool extended, bool repeat, bool up)
{
    return static_cast<LPARAM>(1u | scan << 16 | (extended ? 1u << 24 : 0) | (repeat || up ? 1u << 30 : 0) |
                               (up ? 1u << 31 : 0));
}

static void window_input()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"GraphiteCInputTest";
    RegisterClassW(&wc);
    HWND parent = CreateWindowExW(0, wc.lpszClassName, L"graphite input test", WS_POPUP | WS_VISIBLE, 40, 40, 300, 200,
                                  nullptr, nullptr, wc.hInstance, nullptr);
    CHECK(parent);
    auto scene = gr_scene_new({120, 80});
    auto host = gr_windows_attach_to_hwnd(scene, parent);
    HWND child = static_cast<HWND>(gr_windows_host_get_hwnd(host));
    double scale = GetDpiForWindow(child) / 96.0;
    InputLog log;
    gr_event_handler handler{&log, log_input, nullptr};
    OK(gr_scene_host_set_event_handler(host, &handler));
    pump();

    /* Interactive hosts take mouse input instead of passing it to the parent. */
    POINT center{150, 100};
    ClientToScreen(child, &center);
    CHECK(SendMessageW(child, WM_NCHITTEST, 0, MAKELPARAM(center.x, center.y)) == HTCLIENT);

    /* A press captures the mouse and takes focus; positions are in logical points. */
    SendMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(70, 35));
    CHECK(GetCapture() == child && GetFocus() == child);
    auto down = log.last(GR_EVENT_POINTER_DOWN);
    CHECK(down && down->value.pointer.button == GR_BUTTON_PRIMARY &&
          down->value.pointer.buttons == 1u << GR_BUTTON_PRIMARY && down->value.pointer.click_count == 1);
    CHECK_NEAR(down->value.pointer.position.x, 70 / scale);
    CHECK_NEAR(down->value.pointer.position.y, 35 / scale);
    CHECK(log.last(GR_EVENT_FOCUS_IN));
    SendMessageW(child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(-20, 400));
    auto move = log.last(GR_EVENT_POINTER_MOVE);
    CHECK(move && move->value.pointer.buttons == 1u << GR_BUTTON_PRIMARY);
    CHECK_NEAR(move->value.pointer.position.x, -20 / scale);
    SendMessageW(child, WM_LBUTTONUP, 0, MAKELPARAM(70, 35));
    CHECK(GetCapture() != child);
    auto up = log.last(GR_EVENT_POINTER_UP);
    CHECK(up && up->value.pointer.button == GR_BUTTON_PRIMARY && up->value.pointer.buttons == 0);
    SendMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(71, 35));
    CHECK(log.last(GR_EVENT_POINTER_DOWN)->value.pointer.click_count == 2);
    SendMessageW(child, WM_LBUTTONUP, 0, MAKELPARAM(71, 35));
    CHECK(SendMessageW(child, WM_XBUTTONDOWN, MAKEWPARAM(0, XBUTTON2), MAKELPARAM(5, 5)) == TRUE);
    CHECK(log.last(GR_EVENT_POINTER_DOWN)->value.pointer.button == GR_BUTTON_FORWARD);
    SendMessageW(child, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), MAKELPARAM(5, 5));

    /* Losing the capture mid-drag cancels the pointer. */
    SendMessageW(child, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(10, 10));
    SetCapture(parent);
    CHECK(log.last(GR_EVENT_POINTER_CANCEL));
    ReleaseCapture();

    /* Wheel positions are in screen coordinates; forward scrolls up by one line per WHEEL_DELTA. */
    POINT at{40, 20};
    ClientToScreen(child, &at);
    SendMessageW(child, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(at.x, at.y));
    auto wheel = log.last(GR_EVENT_SCROLL);
    CHECK(wheel && wheel->value.scroll.unit == GR_SCROLL_UNIT_LINE && wheel->value.scroll.delta.y == -1);
    CHECK_NEAR(wheel->value.scroll.position.x, 40 / scale);
    SendMessageW(child, WM_MOUSEHWHEEL, MAKEWPARAM(0, -WHEEL_DELTA / 2), MAKELPARAM(at.x, at.y));
    CHECK(log.last(GR_EVENT_SCROLL)->value.scroll.delta.x == -0.5);

    /* Keys: scan codes give the physical key, virtual keys the named key. */
    SendMessageW(child, WM_KEYDOWN, 'A', key_lparam(0x1E, false, false, false));
    auto k = log.last(GR_EVENT_KEY_DOWN);
    CHECK(k && k->value.key.code == GR_KEY_CODE_A && k->value.key.key == GR_KEY_CHARACTER);
    CHECK(k->value.key.character == 'a' && !k->value.key.repeat && k->value.key.native_code == 0x1E);
    SendMessageW(child, WM_KEYDOWN, 'A', key_lparam(0x1E, false, true, false));
    CHECK(log.last(GR_EVENT_KEY_DOWN)->value.key.repeat);
    SendMessageW(child, WM_KEYUP, 'A', key_lparam(0x1E, false, false, true));
    CHECK(log.last(GR_EVENT_KEY_UP)->value.key.code == GR_KEY_CODE_A);
    SendMessageW(child, WM_KEYDOWN, VK_RETURN, key_lparam(0x1C, true, false, false));
    k = log.last(GR_EVENT_KEY_DOWN);
    CHECK(k->value.key.code == GR_KEY_CODE_NUMPAD_ENTER && k->value.key.key == GR_KEY_ENTER && !k->value.key.character);
    SendMessageW(child, WM_KEYDOWN, VK_CONTROL, key_lparam(0x1D, true, false, false));
    k = log.last(GR_EVENT_KEY_DOWN);
    CHECK(k->value.key.code == GR_KEY_CODE_CONTROL_RIGHT && k->value.key.key == GR_KEY_CONTROL);
    SendMessageW(child, WM_SYSKEYDOWN, VK_F4, key_lparam(0x3E, false, false, false));
    CHECK(log.last(GR_EVENT_KEY_DOWN)->value.key.key == GR_KEY_F4);

    /* Cursors apply over the client area. */
    OK(gr_scene_host_set_cursor(host, GR_CURSOR_TEXT));
    CHECK(SendMessageW(child, WM_SETCURSOR, reinterpret_cast<WPARAM>(child), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE)));
    CHECK(GetCursor() == LoadCursor(nullptr, IDC_IBEAM));
    OK(gr_scene_host_set_cursor(host, GR_CURSOR_DEFAULT));

    gr_scene_host_unref(host);
    CHECK(!IsWindow(child));
    gr_scene_unref(scene);
    DestroyWindow(parent);
    pump();
}

/* A minimal text field model: applies edits, records commands, consumes the Q key and shares its state. */
struct Field
{
    gr_scene_host *host = nullptr;
    std::string text;
    gr_text_range selection{}, composition{};
    gr_text_input_kind kind = GR_TEXT_INPUT_TEXT;
    std::vector<gr_text_command_event> commands;

    void share()
    {
        gr_text_input input{};
        input.kind = kind;
        input.text = {text.data(), text.size()};
        input.selection = selection;
        input.composition = composition;
        input.caret = {{10, 20}, {1, 16}};
        OK(gr_scene_host_set_text_input(host, &input));
    }

    bool is(char const *value, size_t caret) const
    {
        return text == value && selection.start == caret && selection.end == caret;
    }
};

static bool on_field_event(void *userdata, gr_event const *event)
{
    auto &f = *static_cast<Field *>(userdata);
    switch (event->type)
    {
        case GR_EVENT_TEXT_EDIT:
        {
            auto const &e = event->value.edit;
            CHECK(e.replace.end <= f.text.size());
            f.text.replace(e.replace.start, e.replace.end - e.replace.start, e.text.data ? e.text.data : "",
                           e.text.count);
            f.selection = e.selection;
            f.composition = e.composition;
            f.share();
            return true;
        }
        case GR_EVENT_TEXT_COMMAND:
            f.commands.push_back(event->value.command);
            return true;
        case GR_EVENT_KEY_DOWN:
            return event->value.key.character == 'q';
        default:
            return false;
    }
}

static bool has_input_context(HWND hwnd)
{
    HIMC imc = ImmGetContext(hwnd);
    if (imc)
        ImmReleaseContext(hwnd, imc);
    return imc != nullptr;
}

static void set_control(bool down)
{
    BYTE keys[256]{};
    CHECK(GetKeyboardState(keys));
    keys[VK_CONTROL] = down ? 0x80 : 0;
    CHECK(SetKeyboardState(keys));
}

static void window_text_input()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"GraphiteCTextInputTest";
    RegisterClassW(&wc);
    HWND parent = CreateWindowExW(0, wc.lpszClassName, L"graphite text input test", WS_POPUP | WS_VISIBLE, 40, 40, 300,
                                  200, nullptr, nullptr, wc.hInstance, nullptr);
    CHECK(parent);
    auto scene = gr_scene_new({120, 80});
    auto host = gr_windows_attach_to_hwnd(scene, parent);
    HWND child = static_cast<HWND>(gr_windows_host_get_hwnd(host));
    double scale = GetDpiForWindow(child) / 96.0;
    Field f;
    f.host = host;
    gr_event_handler handler{&f, on_field_event, nullptr};
    OK(gr_scene_host_set_event_handler(host, &handler));
    SetFocus(child);
    pump();

    /* Input methods and typing engage only while a field shares its state. */
    CHECK(!has_input_context(child));
    SendMessageW(child, WM_CHAR, 'z', 0);
    CHECK(f.text.empty());
    f.share();
    HIMC imc = ImmGetContext(child);
    CHECK(imc);
    CANDIDATEFORM candidate{};
    CHECK(ImmGetCandidateWindow(imc, 0, &candidate) && candidate.dwStyle == CFS_EXCLUDE);
    CHECK(candidate.rcArea.left == static_cast<LONG>(std::floor(10 * scale)) &&
          candidate.rcArea.top == static_cast<LONG>(std::floor(20 * scale)) &&
          candidate.rcArea.bottom == static_cast<LONG>(std::ceil(36 * scale)));
    ImmReleaseContext(child, imc);

    /* Characters type text, surrogate pairs included; control characters do not. */
    SendMessageW(child, WM_CHAR, 'a', 0);
    SendMessageW(child, WM_CHAR, 'b', 0);
    SendMessageW(child, WM_CHAR, 0xD83D, 0);
    SendMessageW(child, WM_CHAR, 0xDE00, 0);
    SendMessageW(child, WM_CHAR, 0x08, 0);
    CHECK(f.is("ab\xF0\x9F\x98\x80", 6));

    /* A key the handler consumes drops the characters it queued; others type them. */
    PostMessageW(child, WM_CHAR, 'q', 0);
    SendMessageW(child, WM_KEYDOWN, 'Q', key_lparam(0x10, false, false, false));
    pump();
    CHECK(f.is("ab\xF0\x9F\x98\x80", 6));
    PostMessageW(child, WM_CHAR, 'x', 0);
    SendMessageW(child, WM_KEYDOWN, 'X', key_lparam(0x2D, false, false, false));
    pump();
    CHECK(f.is("ab\xF0\x9F\x98\x80x", 7));

    /* Editing keys become commands, following the edit control's bindings. */
    SendMessageW(child, WM_KEYDOWN, VK_BACK, key_lparam(0x0E, false, false, false));
    set_control(true);
    SendMessageW(child, WM_KEYDOWN, VK_LEFT, key_lparam(0x4B, true, false, false));
    SendMessageW(child, WM_KEYDOWN, 'A', key_lparam(0x1E, false, false, false));
    set_control(false);
    SendMessageW(child, WM_KEYDOWN, VK_ESCAPE, key_lparam(0x01, false, false, false));
    CHECK(f.commands.size() == 4);
    CHECK(f.commands[0].command == GR_TEXT_COMMAND_DELETE_BACKWARD &&
          f.commands[1].command == GR_TEXT_COMMAND_MOVE_WORD_LEFT &&
          f.commands[2].command == GR_TEXT_COMMAND_SELECT_ALL && f.commands[3].command == GR_TEXT_COMMAND_CANCEL);

    /* Input methods find characters on screen; without a layout every character is at the shared caret. */
    IMECHARPOSITION position{sizeof(position)};
    CHECK(SendMessageW(child, WM_IME_REQUEST, IMR_QUERYCHARPOSITION, reinterpret_cast<LPARAM>(&position)) == TRUE);
    POINT caret{static_cast<LONG>(std::floor(10 * scale)), static_cast<LONG>(std::floor(20 * scale))};
    ClientToScreen(child, &caret);
    CHECK(position.pt.x == caret.x && position.pt.y == caret.y && position.cLineHeight > 0);

    /* The input method's own composition window stays hidden; a cancel without a composition changes nothing. */
    CHECK(SendMessageW(child, WM_IME_STARTCOMPOSITION, 0, 0) == 0);
    SendMessageW(child, WM_IME_COMPOSITION, 0, 0);
    SendMessageW(child, WM_IME_ENDCOMPOSITION, 0, 0);
    CHECK(f.is("ab\xF0\x9F\x98\x80x", 7));

    /* Passwords take typing but no input method. */
    f.kind = GR_TEXT_INPUT_PASSWORD;
    f.share();
    CHECK(!has_input_context(child));
    SendMessageW(child, WM_CHAR, 'p', 0);
    CHECK(f.is("ab\xF0\x9F\x98\x80xp", 8));
    f.kind = GR_TEXT_INPUT_TEXT;
    f.share();
    CHECK(has_input_context(child));

    OK(gr_scene_host_set_text_input(host, nullptr));
    CHECK(!has_input_context(child));
    SendMessageW(child, WM_CHAR, 'z', 0);
    CHECK(f.text.size() == 8);

    gr_scene_host_unref(host);
    gr_scene_unref(scene);
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
    const gr_test_case cases[] = {GR_TEST_CASE(window_hosting),   GR_TEST_CASE(offscreen_alpha_and_lifetime),
                                  GR_TEST_CASE(visual_hosting),   GR_TEST_CASE(invalid_arguments),
                                  GR_TEST_CASE(window_events),    GR_TEST_CASE(window_input),
                                  GR_TEST_CASE(window_text_input)};
    return gr_test_run("windows", cases, sizeof(cases) / sizeof(cases[0]), argc, argv);
}
