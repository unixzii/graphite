#include "internal.hpp"

#include <dcomp.h>
#include <imm.h>
#include <mutex>
#include <string>
#include <string_view>
#include <windowsx.h>

namespace gr_win
{
constexpr UINT frame_message = WM_APP + 1;

struct WindowData
{
    gr_scene_link *scene;
    bool track;
    bool *creation_claimed;
    gr_scene_host *host = nullptr; /* Borrowed; cleared before the host is destroyed. */
    /* Input state. */
    uint32_t buttons = 0; /* Pressed buttons, 1u << gr_button. */
    bool inside = false, tracking = false;
    uint32_t clicks = 0;
    gr_button click_button = GR_BUTTON_NONE;
    DWORD click_time = 0;
    POINT click_point{};
    HCURSOR cursor = LoadCursor(nullptr, IDC_ARROW);
    /* Text input: the shared state with this window's edits applied; text_input.text borrows text and its layout
     * holds a reference. */
    bool text_on = false, resetting = false;
    gr_text_input text_input{};
    std::string text;
    wchar_t high_surrogate = 0;

    ~WindowData()
    {
        gr_text_layout_unref(text_input.layout);
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
static bool dispatch(WindowData *data, gr_event const &event)
{
    return data && data->host && gr_scene_host_dispatch_event(data->host, &event);
}

/* Input. Messages arrive with the host referenced, so the window and its data outlive each dispatch. */

static gr_modifiers key_modifiers()
{
    auto down = [](int vk) { return (GetKeyState(vk) & 0x8000) != 0; };
    return (down(VK_SHIFT) ? GR_MODIFIER_SHIFT : 0) | (down(VK_CONTROL) ? GR_MODIFIER_CONTROL : 0) |
           (down(VK_MENU) ? GR_MODIFIER_ALT : 0) | (down(VK_LWIN) || down(VK_RWIN) ? GR_MODIFIER_SUPER : 0) |
           (GetKeyState(VK_CAPITAL) & 1 ? GR_MODIFIER_CAPS_LOCK : 0);
}

static double message_seconds()
{
    return static_cast<DWORD>(GetMessageTime()) / 1000.0;
}

static gr_event pointer_event(HWND hwnd, WindowData const &d, gr_event_type type, POINT p)
{
    /* Pen input promoted to mouse messages carries a signature; touch sets bit 7 as well. */
    auto extra = static_cast<uint64_t>(GetMessageExtraInfo());
    bool pen = (extra & 0xFFFFFF00) == 0xFF515700 && !(extra & 0x80);
    double scale = window_scale(hwnd);
    gr_event e{};
    e.type = type;
    e.time = message_seconds();
    e.modifiers = key_modifiers();
    e.value.pointer.kind = pen ? GR_POINTER_PEN : GR_POINTER_MOUSE;
    e.value.pointer.position = {p.x / scale, p.y / scale};
    e.value.pointer.buttons = d.buttons;
    e.value.pointer.pressure = d.buttons ? 0.5f : 0;
    return e;
}

static void hover(HWND hwnd, WindowData &d, bool inside, POINT p)
{
    if (inside == d.inside)
        return;
    d.inside = inside;
    dispatch(&d, pointer_event(hwnd, d, inside ? GR_EVENT_POINTER_ENTER : GR_EVENT_POINTER_LEAVE, p));
}

static gr_button message_button(UINT message, WPARAM wp)
{
    switch (message)
    {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            return GR_BUTTON_PRIMARY;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
            return GR_BUTTON_SECONDARY;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
            return GR_BUTTON_MIDDLE;
        default:
            return GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? GR_BUTTON_BACK : GR_BUTTON_FORWARD;
    }
}

/* Counts consecutive presses of one button within the double-click time and rectangle. */
static uint32_t count_click(WindowData &d, gr_button button, POINT p)
{
    DWORD time = static_cast<DWORD>(GetMessageTime());
    bool repeat = d.clicks && button == d.click_button && time - d.click_time <= GetDoubleClickTime() &&
                  std::abs(p.x - d.click_point.x) <= GetSystemMetrics(SM_CXDOUBLECLK) / 2 &&
                  std::abs(p.y - d.click_point.y) <= GetSystemMetrics(SM_CYDOUBLECLK) / 2;
    d.clicks = repeat ? d.clicks + 1 : 1;
    d.click_button = button;
    d.click_time = time;
    d.click_point = p;
    return d.clicks;
}

static bool client_contains(HWND hwnd, POINT p)
{
    RECT r{};
    GetClientRect(hwnd, &r);
    return PtInRect(&r, p) != 0;
}

/* A press captures the mouse, so moves and the release reach the host until every button is up. */
static bool press(HWND hwnd, WindowData &d, UINT message, WPARAM wp, LPARAM lp, bool down)
{
    gr_button button = message_button(message, wp);
    POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    if (down)
    {
        if (!d.buttons)
            SetCapture(hwnd);
        if (GetFocus() != hwnd)
            SetFocus(hwnd);
    }
    uint32_t bit = 1u << button;
    d.buttons = down ? d.buttons | bit : d.buttons & ~bit;
    if (!down && !d.buttons && GetCapture() == hwnd)
        ReleaseCapture();
    gr_event e = pointer_event(hwnd, d, down ? GR_EVENT_POINTER_DOWN : GR_EVENT_POINTER_UP, p);
    e.value.pointer.button = button;
    e.value.pointer.click_count = down ? count_click(d, button, p) : d.clicks;
    bool handled = dispatch(&d, e);
    /* Crossings are not reported during drags; catch up once the last button is released. */
    if (!d.buttons)
        hover(hwnd, d, client_contains(hwnd, p), p);
    return handled;
}

/* Text input. The window keeps a copy of the shared state (see host.h) and turns typed characters, IMM32
 * compositions and editing keys into TEXT_EDIT and TEXT_COMMAND events. The window has no input context while text
 * input is off or for passwords, so input methods only engage for text. */

static bool composing(WindowData const &d)
{
    return d.text_on && d.text_input.composition.start < d.text_input.composition.end;
}

/* Replaces the stored state with a copy of input, or clears it with nullptr. */
static void store_text_input(WindowData &d, gr_text_input const *input)
{
    gr_text_layout *layout = input ? gr_text_layout_ref(input->layout) : nullptr;
    gr_text_layout_unref(d.text_input.layout);
    d.text.assign(input && input->text.count ? input->text.data : "", input ? input->text.count : 0);
    d.text_on = input != nullptr;
    d.text_input = input ? *input : gr_text_input{};
    d.text_input.text = {d.text.data(), d.text.size()};
    d.text_input.layout = layout;
    d.high_surrogate = 0;
}

static std::string to_utf8(wchar_t const *text, size_t count)
{
    std::string out;
    if (!count)
        return out;
    int n = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(count), nullptr, 0, nullptr, nullptr);
    out.resize(static_cast<size_t>(std::max(n, 0)));
    WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(count), out.data(), n, nullptr, nullptr);
    return out;
}

/* Byte offset reached by advancing units UTF-16 code units from the byte offset at in UTF-8 text. */
static size_t advance_utf16(std::string_view text, size_t at, size_t units)
{
    while (units && at < text.size())
    {
        auto lead = static_cast<unsigned char>(text[at]);
        size_t bytes = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        units -= std::min<size_t>(units, bytes == 4 ? 2 : 1);
        at = std::min(at + bytes, text.size());
    }
    return at;
}

/* Applies one of this window's edits to the stored state, then delivers it. */
static void text_edit(WindowData &d, gr_text_range replace, std::string_view text, gr_text_range selection,
                      gr_text_range composition)
{
    std::string inserted(text); /* d.text changes below. */
    d.text.replace(replace.start, replace.end - replace.start, inserted);
    d.text_input.text = {d.text.data(), d.text.size()};
    d.text_input.selection = selection;
    d.text_input.composition = composition;
    gr_event e{};
    e.type = GR_EVENT_TEXT_EDIT;
    e.time = message_seconds();
    e.value.edit = {replace, {inserted.data(), inserted.size()}, selection, composition};
    dispatch(&d, e);
}

/* What typing replaces: the composition, or else the selection. */
static gr_text_range typing_target(WindowData const &d)
{
    return composing(d) ? d.text_input.composition : d.text_input.selection;
}

static void insert_text(WindowData &d, std::string_view text)
{
    gr_text_range target = typing_target(d);
    size_t end = target.start + text.size();
    text_edit(d, target, text, {end, end}, {end, end});
}

/* Typed characters, which arrive as UTF-16 code units; control characters come through as editing keys instead. */
static bool text_char(WindowData &d, WPARAM wp)
{
    if (!d.text_on)
        return false;
    auto c = static_cast<wchar_t>(wp);
    if (IS_HIGH_SURROGATE(c))
    {
        d.high_surrogate = c;
        return true;
    }
    wchar_t units[2] = {c, 0};
    size_t count = 1;
    if (IS_LOW_SURROGATE(c))
    {
        if (!d.high_surrogate)
            return true;
        units[0] = d.high_surrogate;
        units[1] = c;
        count = 2;
    }
    d.high_surrogate = 0;
    if (count == 1 && (c < 0x20 || c == 0x7F))
        return true;
    insert_text(d, to_utf8(units, count));
    return true;
}

/* Host-space rectangle of the character at offset, or its caret at the end, from the shared layout; without one,
 * the shared caret. */
static gr_rect character_rect(WindowData const &d, size_t offset)
{
    gr_text_input const &in = d.text_input;
    if (!in.layout)
        return in.caret;
    size_t next = advance_utf16(d.text, offset, 1);
    gr_rect r{};
    if (next <= offset || !gr_text_layout_get_range_rects(in.layout, {offset, next}, &r, 1))
        gr_text_layout_get_caret_rect(in.layout, {offset, GR_TEXT_AFFINITY_DOWNSTREAM}, &r);
    r.origin.x += in.origin.x;
    r.origin.y += in.origin.y;
    return r;
}

/* A host-space rectangle in client pixels. */
static RECT client_rect(HWND hwnd, gr_rect r)
{
    double scale = window_scale(hwnd);
    return {static_cast<LONG>(std::floor(r.origin.x * scale)), static_cast<LONG>(std::floor(r.origin.y * scale)),
            static_cast<LONG>(std::ceil((r.origin.x + std::max(r.size.width, 1.0)) * scale)),
            static_cast<LONG>(std::ceil((r.origin.y + r.size.height) * scale))};
}

/* Input methods place their windows by the caret, at the start of the composition while there is one. */
static void place_input_method(HWND hwnd, WindowData const &d)
{
    HIMC imc = ImmGetContext(hwnd);
    if (!imc)
        return;
    RECT caret = client_rect(
        hwnd, character_rect(d, composing(d) ? d.text_input.composition.start : d.text_input.selection.end));
    COMPOSITIONFORM composition{CFS_POINT, {caret.left, caret.top}, {}};
    ImmSetCompositionWindow(imc, &composition);
    CANDIDATEFORM candidate{0, CFS_EXCLUDE, {caret.left, caret.bottom}, caret};
    ImmSetCandidateWindow(imc, &candidate);
    ImmReleaseContext(hwnd, imc);
}

/* Drops the input method's composition without hearing about it. */
static void cancel_composition(HWND hwnd, WindowData &d)
{
    HIMC imc = ImmGetContext(hwnd);
    if (!imc)
        return;
    d.resetting = true;
    ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
    d.resetting = false;
    ImmReleaseContext(hwnd, imc);
}

static std::wstring composition_string(HIMC imc, DWORD kind)
{
    LONG bytes = ImmGetCompositionStringW(imc, kind, nullptr, 0);
    if (bytes <= 0)
        return {};
    std::wstring out(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
    ImmGetCompositionStringW(imc, kind, out.data(), static_cast<DWORD>(bytes));
    return out;
}

/* WM_IME_COMPOSITION: a committed result, the composition so far, or neither when the composition is cancelled. */
static void ime_composition(HWND hwnd, WindowData &d, LPARAM lp)
{
    if (!d.text_on || d.resetting)
        return;
    HIMC imc = ImmGetContext(hwnd);
    if (!imc)
        return;
    if (lp & GCS_RESULTSTR)
    {
        std::wstring result = composition_string(imc, GCS_RESULTSTR);
        insert_text(d, to_utf8(result.data(), result.size()));
    }
    if ((lp & GCS_COMPSTR) && d.text_on)
    {
        std::wstring units = composition_string(imc, GCS_COMPSTR);
        if (!units.empty() || composing(d))
        {
            std::string text = to_utf8(units.data(), units.size());
            size_t at = typing_target(d).start;
            size_t cursor =
                lp & GCS_CURSORPOS
                    ? static_cast<size_t>(std::max(ImmGetCompositionStringW(imc, GCS_CURSORPOS, nullptr, 0), 0L))
                    : units.size();
            gr_text_range selection = {at + advance_utf16(text, 0, cursor), at + advance_utf16(text, 0, cursor)};
            /* While converting (as in Japanese), the clause being converted is selected. */
            std::string attributes(
                static_cast<size_t>(std::max(ImmGetCompositionStringW(imc, GCS_COMPATTR, nullptr, 0), 0L)), '\0');
            if (!attributes.empty())
                ImmGetCompositionStringW(imc, GCS_COMPATTR, attributes.data(), static_cast<DWORD>(attributes.size()));
            size_t first = attributes.find(static_cast<char>(ATTR_TARGET_CONVERTED));
            if (first != std::string::npos)
            {
                size_t last = attributes.find_last_of(static_cast<char>(ATTR_TARGET_CONVERTED));
                selection = {at + advance_utf16(text, 0, first), at + advance_utf16(text, 0, last + 1)};
            }
            text_edit(d, typing_target(d), text, selection, {at, at + text.size()});
        }
    }
    else if (!(lp & (GCS_RESULTSTR | GCS_COMPSTR)) && composing(d))
    {
        gr_text_range c = d.text_input.composition;
        text_edit(d, c, {}, {c.start, c.start}, {c.start, c.start});
    }
    ImmReleaseContext(hwnd, imc);
    if (d.text_on)
        place_input_method(hwnd, d);
}

/* WM_IME_ENDCOMPOSITION without a result: the input method cancelled its composition. */
static void ime_end(WindowData &d)
{
    if (!composing(d) || d.resetting)
        return;
    gr_text_range c = d.text_input.composition;
    text_edit(d, c, {}, {c.start, c.start}, {c.start, c.start});
}

/* IMR_QUERYCHARPOSITION: the screen position of a composition character, in UTF-16 units from its start. */
static LRESULT query_char_position(HWND hwnd, WindowData const &d, IMECHARPOSITION *query)
{
    if (!d.text_on || !query || query->dwSize < sizeof(IMECHARPOSITION))
        return 0;
    size_t start = composing(d) ? d.text_input.composition.start : d.text_input.selection.end;
    RECT r = client_rect(hwnd, character_rect(d, advance_utf16(d.text, start, query->dwCharPos)));
    POINT top{r.left, r.top};
    ClientToScreen(hwnd, &top);
    query->pt = top;
    query->cLineHeight = static_cast<UINT>(r.bottom - r.top);
    RECT document{};
    GetClientRect(hwnd, &document);
    MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT *>(&document), 2);
    query->rcDocument = document;
    return TRUE;
}

/* Editing key bindings, following the Windows edit control. */
static bool key_command(unsigned vk, gr_text_command_event &out)
{
    auto down = [](int key) { return (GetKeyState(key) & 0x8000) != 0; };
    bool shift = down(VK_SHIFT), control = down(VK_CONTROL);
    if (down(VK_MENU) || down(VK_LWIN) || down(VK_RWIN))
        return false;
    gr_text_command command;
    bool extend = false;
    switch (vk)
    {
        case VK_BACK:
            command = control ? GR_TEXT_COMMAND_DELETE_WORD_BACKWARD : GR_TEXT_COMMAND_DELETE_BACKWARD;
            break;
        case VK_DELETE:
            if (shift && !control)
                return false; /* Cut. */
            command = control ? GR_TEXT_COMMAND_DELETE_WORD_FORWARD : GR_TEXT_COMMAND_DELETE_FORWARD;
            break;
        case VK_LEFT:
            command = control ? GR_TEXT_COMMAND_MOVE_WORD_LEFT : GR_TEXT_COMMAND_MOVE_LEFT;
            extend = shift;
            break;
        case VK_RIGHT:
            command = control ? GR_TEXT_COMMAND_MOVE_WORD_RIGHT : GR_TEXT_COMMAND_MOVE_RIGHT;
            extend = shift;
            break;
        case VK_UP:
            command = GR_TEXT_COMMAND_MOVE_UP;
            extend = shift;
            break;
        case VK_DOWN:
            command = GR_TEXT_COMMAND_MOVE_DOWN;
            extend = shift;
            break;
        case VK_HOME:
            command = control ? GR_TEXT_COMMAND_MOVE_DOCUMENT_START : GR_TEXT_COMMAND_MOVE_LINE_START;
            extend = shift;
            break;
        case VK_END:
            command = control ? GR_TEXT_COMMAND_MOVE_DOCUMENT_END : GR_TEXT_COMMAND_MOVE_LINE_END;
            extend = shift;
            break;
        case VK_PRIOR:
            command = GR_TEXT_COMMAND_MOVE_PAGE_UP;
            extend = shift;
            break;
        case VK_NEXT:
            command = GR_TEXT_COMMAND_MOVE_PAGE_DOWN;
            extend = shift;
            break;
        case VK_RETURN:
            if (control)
                return false;
            command = GR_TEXT_COMMAND_INSERT_NEWLINE;
            break;
        case VK_TAB:
            if (control)
                return false;
            command = shift ? GR_TEXT_COMMAND_INSERT_BACKTAB : GR_TEXT_COMMAND_INSERT_TAB;
            break;
        case VK_ESCAPE:
            if (shift || control)
                return false;
            command = GR_TEXT_COMMAND_CANCEL;
            break;
        case 'A':
            if (!control || shift)
                return false;
            command = GR_TEXT_COMMAND_SELECT_ALL;
            break;
        default:
            return false;
    }
    out = {command, extend};
    return true;
}

/* Removes the characters TranslateMessage queued for a key the host consumed. */
static void drop_characters(HWND hwnd)
{
    MSG m{};
    while (PeekMessageW(&m, hwnd, WM_CHAR, WM_DEADCHAR, PM_REMOVE) ||
           PeekMessageW(&m, hwnd, WM_SYSCHAR, WM_SYSDEADCHAR, PM_REMOVE))
    {
    }
}

void set_window_text_input(HWND hwnd, gr_text_input const *input)
{
    auto data = window_data(hwnd);
    if (!data)
        return;
    WindowData &d = *data;
    bool restart = input && (!d.text_on || input->kind != d.text_input.kind);
    bool recompose = input && d.text_on &&
                     (input->composition.start != d.text_input.composition.start ||
                      input->composition.end != d.text_input.composition.end);
    if ((composing(d) && !input) || recompose)
        cancel_composition(hwnd, d);
    store_text_input(d, input);
    if (!input || restart)
        ImmAssociateContextEx(hwnd, nullptr, input && input->kind != GR_TEXT_INPUT_PASSWORD ? IACE_DEFAULT : 0);
    if (input)
        place_input_method(hwnd, d);
}

static bool key(HWND hwnd, WindowData &d, UINT message, WPARAM wp, LPARAM lp)
{
    bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    auto vk = static_cast<unsigned>(wp);
    unsigned scan = (static_cast<unsigned>(lp) >> 16 & 0xFF) | (lp & (1 << 24) ? 0xE000 : 0);
    gr_key named = named_key(vk);
    uint32_t character = 0;
    if (named == GR_KEY_UNKNOWN)
    {
        /* The text the key types on the thread's layout without modifiers; bit 31 marks dead keys. */
        UINT c = MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR) & 0x7FFFFFFF;
        if (c >= 0x20 && c != 0x7F && c <= 0xFFFF)
        {
            wchar_t lower = static_cast<wchar_t>(c);
            CharLowerBuffW(&lower, 1);
            character = lower;
            named = GR_KEY_CHARACTER;
        }
    }
    gr_event e{};
    e.type = down ? GR_EVENT_KEY_DOWN : GR_EVENT_KEY_UP;
    e.time = message_seconds();
    e.modifiers = key_modifiers();
    e.value.key.code = key_code(scan);
    e.value.key.key = named;
    e.value.key.character = character;
    e.value.key.repeat = down && (lp & (1 << 30)) != 0;
    /* VK_PROCESSKEY marks keys an input method took before the window saw them. */
    bool held = composing(d) || vk == VK_PROCESSKEY;
    e.value.key.composing = held;
    e.value.key.native_code = scan;
    if (dispatch(&d, e) && !held)
    {
        drop_characters(hwnd);
        return true;
    }
    /* Keys the handler leaves type text (through WM_CHAR) or edit it. */
    gr_text_command_event command{};
    if (!down || held || !d.text_on || !key_command(vk, command))
        return false;
    gr_event c{};
    c.type = GR_EVENT_TEXT_COMMAND;
    c.time = e.time;
    c.modifiers = e.modifiers;
    c.value.command = command;
    if (!dispatch(&d, c))
        return false;
    drop_characters(hwnd);
    return true;
}

/* Handles an input message of an interactive host; returns false to leave it to DefWindowProc. */
static bool input(HWND hwnd, WindowData &d, UINT message, WPARAM wp, LPARAM lp, LRESULT &result)
{
    switch (message)
    {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP:
        {
            bool down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN ||
                        message == WM_XBUTTONDOWN;
            result = message == WM_XBUTTONDOWN || message == WM_XBUTTONUP;
            return press(hwnd, d, message, wp, lp, down);
        }
        case WM_MOUSEMOVE:
        {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (!d.tracking)
            {
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
                d.tracking = TrackMouseEvent(&track) != 0;
            }
            if (!d.buttons)
                hover(hwnd, d, true, p);
            return dispatch(&d, pointer_event(hwnd, d, GR_EVENT_POINTER_MOVE, p));
        }
        case WM_MOUSELEAVE:
            d.tracking = false;
            if (!d.buttons)
            {
                POINT p{};
                GetCursorPos(&p);
                ScreenToClient(hwnd, &p);
                hover(hwnd, d, false, p);
            }
            return true;
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lp) != hwnd && d.buttons)
            {
                d.buttons = 0;
                POINT p{};
                GetCursorPos(&p);
                ScreenToClient(hwnd, &p);
                dispatch(&d, pointer_event(hwnd, d, GR_EVENT_POINTER_CANCEL, p));
            }
            return true;
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        {
            /* Wheel positions are in screen coordinates; WHEEL_DELTA is one line, and forward scrolls up. */
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            double lines = GET_WHEEL_DELTA_WPARAM(wp) / static_cast<double>(WHEEL_DELTA);
            gr_event e{};
            e.type = GR_EVENT_SCROLL;
            e.time = message_seconds();
            e.modifiers = key_modifiers();
            double scale = window_scale(hwnd);
            e.value.scroll.position = {p.x / scale, p.y / scale};
            e.value.scroll.delta = message == WM_MOUSEWHEEL ? gr_point{0, -lines} : gr_point{lines, 0};
            e.value.scroll.unit = GR_SCROLL_UNIT_LINE;
            return dispatch(&d, e); /* Unhandled wheel messages go on to the parent. */
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
            return key(hwnd, d, message, wp, lp);
        case WM_CHAR:
            return text_char(d, wp);
        case WM_IME_SETCONTEXT:
            /* The host draws compositions inline; the input method keeps its candidate window. */
            result = DefWindowProcW(hwnd, message, wp, d.text_on ? lp & ~ISC_SHOWUICOMPOSITIONWINDOW : lp);
            return true;
        case WM_IME_STARTCOMPOSITION:
            if (!d.text_on)
                return false;
            place_input_method(hwnd, d);
            return true;
        case WM_IME_COMPOSITION:
            if (!d.text_on)
                return false;
            ime_composition(hwnd, d, lp);
            return true;
        case WM_IME_ENDCOMPOSITION:
            if (!d.text_on)
                return false;
            ime_end(d);
            return true;
        case WM_IME_CHAR:
            return d.text_on; /* Results arrive with WM_IME_COMPOSITION. */
        case WM_IME_REQUEST:
            if (wp != IMR_QUERYCHARPOSITION)
                return false;
            result = query_char_position(hwnd, d, reinterpret_cast<IMECHARPOSITION *>(lp));
            return true;
        case WM_SETCURSOR:
            if (LOWORD(lp) != HTCLIENT)
                return false;
            SetCursor(d.cursor);
            result = TRUE;
            return true;
        default:
            return false;
    }
}

static bool is_input(UINT message)
{
    return (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || message == WM_MOUSELEAVE ||
           message == WM_CAPTURECHANGED || message == WM_SETCURSOR ||
           (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
           (message >= WM_IME_STARTCOMPOSITION && message <= WM_IME_COMPOSITION) || message == WM_IME_SETCONTEXT ||
           message == WM_IME_CHAR || message == WM_IME_REQUEST;
}

void set_window_cursor(HWND hwnd, gr_cursor cursor)
{
    auto data = window_data(hwnd);
    if (!data)
        return;
    static const LPCTSTR ids[] = {
        IDC_ARROW,  IDC_HAND,   IDC_IBEAM,    IDC_CROSS,    IDC_HAND, IDC_HAND,
        IDC_SIZEWE, IDC_SIZENS, IDC_SIZENESW, IDC_SIZENWSE, IDC_NO,   nullptr,
    };
    static_assert(sizeof(ids) / sizeof(ids[0]) == GR_CURSOR_NONE + 1, "one entry per gr_cursor");
    data->cursor = ids[cursor] ? LoadCursor(nullptr, ids[cursor]) : nullptr;
    POINT p{};
    if (GetCapture() == hwnd || (GetCursorPos(&p) && WindowFromPoint(p) == hwnd))
        SetCursor(data->cursor);
}

void set_window_host(HWND hwnd, gr_scene_host *host)
{
    auto data = window_data(hwnd);
    if (!data)
        return;
    set_window_text_input(hwnd, nullptr);
    data->host = host;
    /* Input methods engage once a text field turns text input on. */
    ImmAssociateContextEx(hwnd, nullptr, 0);
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
        if (is_input(message))
            if (auto data = window_data(hwnd); data && data->host)
            {
                /* Hold the host so a handler releasing it cannot destroy this window mid-message. */
                gr_scene_host *host = gr_scene_host_ref(data->host);
                LRESULT result = 0;
                if (!input(hwnd, *data, message, wp, lp, result))
                    result = DefWindowProcW(hwnd, message, wp, lp);
                gr_scene_host_unref(host);
                return result;
            }
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
            {
                /* Interactive hosts take mouse input; others let it through to the parent. */
                auto data = window_data(hwnd);
                if (data && data->host)
                    break;
                return HTTRANSPARENT;
            }
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
