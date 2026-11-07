#include "internal.h"

#if __has_include(<linux/input-event-codes.h>)
#include <linux/input-event-codes.h>

#define HAVE_EVDEV 1
#endif

#ifdef HAVE_EVDEV
/* Indexed by evdev code; X11 and Wayland keycodes are evdev codes plus 8. */
static const gr_key_code codes[256] = {
    [KEY_A] = GR_KEY_CODE_A,
    [KEY_B] = GR_KEY_CODE_B,
    [KEY_C] = GR_KEY_CODE_C,
    [KEY_D] = GR_KEY_CODE_D,
    [KEY_E] = GR_KEY_CODE_E,
    [KEY_F] = GR_KEY_CODE_F,
    [KEY_G] = GR_KEY_CODE_G,
    [KEY_H] = GR_KEY_CODE_H,
    [KEY_I] = GR_KEY_CODE_I,
    [KEY_J] = GR_KEY_CODE_J,
    [KEY_K] = GR_KEY_CODE_K,
    [KEY_L] = GR_KEY_CODE_L,
    [KEY_M] = GR_KEY_CODE_M,
    [KEY_N] = GR_KEY_CODE_N,
    [KEY_O] = GR_KEY_CODE_O,
    [KEY_P] = GR_KEY_CODE_P,
    [KEY_Q] = GR_KEY_CODE_Q,
    [KEY_R] = GR_KEY_CODE_R,
    [KEY_S] = GR_KEY_CODE_S,
    [KEY_T] = GR_KEY_CODE_T,
    [KEY_U] = GR_KEY_CODE_U,
    [KEY_V] = GR_KEY_CODE_V,
    [KEY_W] = GR_KEY_CODE_W,
    [KEY_X] = GR_KEY_CODE_X,
    [KEY_Y] = GR_KEY_CODE_Y,
    [KEY_Z] = GR_KEY_CODE_Z,
    [KEY_0] = GR_KEY_CODE_DIGIT_0,
    [KEY_1] = GR_KEY_CODE_DIGIT_1,
    [KEY_2] = GR_KEY_CODE_DIGIT_2,
    [KEY_3] = GR_KEY_CODE_DIGIT_3,
    [KEY_4] = GR_KEY_CODE_DIGIT_4,
    [KEY_5] = GR_KEY_CODE_DIGIT_5,
    [KEY_6] = GR_KEY_CODE_DIGIT_6,
    [KEY_7] = GR_KEY_CODE_DIGIT_7,
    [KEY_8] = GR_KEY_CODE_DIGIT_8,
    [KEY_9] = GR_KEY_CODE_DIGIT_9,
    [KEY_MINUS] = GR_KEY_CODE_MINUS,
    [KEY_EQUAL] = GR_KEY_CODE_EQUAL,
    [KEY_LEFTBRACE] = GR_KEY_CODE_BRACKET_LEFT,
    [KEY_RIGHTBRACE] = GR_KEY_CODE_BRACKET_RIGHT,
    [KEY_BACKSLASH] = GR_KEY_CODE_BACKSLASH,
    [KEY_SEMICOLON] = GR_KEY_CODE_SEMICOLON,
    [KEY_APOSTROPHE] = GR_KEY_CODE_QUOTE,
    [KEY_GRAVE] = GR_KEY_CODE_BACKQUOTE,
    [KEY_COMMA] = GR_KEY_CODE_COMMA,
    [KEY_DOT] = GR_KEY_CODE_PERIOD,
    [KEY_SLASH] = GR_KEY_CODE_SLASH,
    [KEY_102ND] = GR_KEY_CODE_INTL_BACKSLASH,
    [KEY_RO] = GR_KEY_CODE_INTL_RO,
    [KEY_YEN] = GR_KEY_CODE_INTL_YEN,
    [KEY_ENTER] = GR_KEY_CODE_ENTER,
    [KEY_ESC] = GR_KEY_CODE_ESCAPE,
    [KEY_BACKSPACE] = GR_KEY_CODE_BACKSPACE,
    [KEY_TAB] = GR_KEY_CODE_TAB,
    [KEY_SPACE] = GR_KEY_CODE_SPACE,
    [KEY_CAPSLOCK] = GR_KEY_CODE_CAPS_LOCK,
    [KEY_LEFTSHIFT] = GR_KEY_CODE_SHIFT_LEFT,
    [KEY_RIGHTSHIFT] = GR_KEY_CODE_SHIFT_RIGHT,
    [KEY_LEFTCTRL] = GR_KEY_CODE_CONTROL_LEFT,
    [KEY_RIGHTCTRL] = GR_KEY_CODE_CONTROL_RIGHT,
    [KEY_LEFTALT] = GR_KEY_CODE_ALT_LEFT,
    [KEY_RIGHTALT] = GR_KEY_CODE_ALT_RIGHT,
    [KEY_LEFTMETA] = GR_KEY_CODE_META_LEFT,
    [KEY_RIGHTMETA] = GR_KEY_CODE_META_RIGHT,
    [KEY_COMPOSE] = GR_KEY_CODE_CONTEXT_MENU,
    [KEY_LEFT] = GR_KEY_CODE_ARROW_LEFT,
    [KEY_RIGHT] = GR_KEY_CODE_ARROW_RIGHT,
    [KEY_UP] = GR_KEY_CODE_ARROW_UP,
    [KEY_DOWN] = GR_KEY_CODE_ARROW_DOWN,
    [KEY_INSERT] = GR_KEY_CODE_INSERT,
    [KEY_DELETE] = GR_KEY_CODE_DELETE,
    [KEY_HOME] = GR_KEY_CODE_HOME,
    [KEY_END] = GR_KEY_CODE_END,
    [KEY_PAGEUP] = GR_KEY_CODE_PAGE_UP,
    [KEY_PAGEDOWN] = GR_KEY_CODE_PAGE_DOWN,
    [KEY_SYSRQ] = GR_KEY_CODE_PRINT_SCREEN,
    [KEY_SCROLLLOCK] = GR_KEY_CODE_SCROLL_LOCK,
    [KEY_PAUSE] = GR_KEY_CODE_PAUSE,
    [KEY_NUMLOCK] = GR_KEY_CODE_NUM_LOCK,
    [KEY_KP0] = GR_KEY_CODE_NUMPAD_0,
    [KEY_KP1] = GR_KEY_CODE_NUMPAD_1,
    [KEY_KP2] = GR_KEY_CODE_NUMPAD_2,
    [KEY_KP3] = GR_KEY_CODE_NUMPAD_3,
    [KEY_KP4] = GR_KEY_CODE_NUMPAD_4,
    [KEY_KP5] = GR_KEY_CODE_NUMPAD_5,
    [KEY_KP6] = GR_KEY_CODE_NUMPAD_6,
    [KEY_KP7] = GR_KEY_CODE_NUMPAD_7,
    [KEY_KP8] = GR_KEY_CODE_NUMPAD_8,
    [KEY_KP9] = GR_KEY_CODE_NUMPAD_9,
    [KEY_KPPLUS] = GR_KEY_CODE_NUMPAD_ADD,
    [KEY_KPMINUS] = GR_KEY_CODE_NUMPAD_SUBTRACT,
    [KEY_KPASTERISK] = GR_KEY_CODE_NUMPAD_MULTIPLY,
    [KEY_KPSLASH] = GR_KEY_CODE_NUMPAD_DIVIDE,
    [KEY_KPDOT] = GR_KEY_CODE_NUMPAD_DECIMAL,
    [KEY_KPCOMMA] = GR_KEY_CODE_NUMPAD_COMMA,
    [KEY_KPJPCOMMA] = GR_KEY_CODE_NUMPAD_COMMA,
    [KEY_KPEQUAL] = GR_KEY_CODE_NUMPAD_EQUAL,
    [KEY_KPENTER] = GR_KEY_CODE_NUMPAD_ENTER,
    [KEY_F1] = GR_KEY_CODE_F1,
    [KEY_F2] = GR_KEY_CODE_F2,
    [KEY_F3] = GR_KEY_CODE_F3,
    [KEY_F4] = GR_KEY_CODE_F4,
    [KEY_F5] = GR_KEY_CODE_F5,
    [KEY_F6] = GR_KEY_CODE_F6,
    [KEY_F7] = GR_KEY_CODE_F7,
    [KEY_F8] = GR_KEY_CODE_F8,
    [KEY_F9] = GR_KEY_CODE_F9,
    [KEY_F10] = GR_KEY_CODE_F10,
    [KEY_F11] = GR_KEY_CODE_F11,
    [KEY_F12] = GR_KEY_CODE_F12,
    [KEY_F13] = GR_KEY_CODE_F13,
    [KEY_F14] = GR_KEY_CODE_F14,
    [KEY_F15] = GR_KEY_CODE_F15,
    [KEY_F16] = GR_KEY_CODE_F16,
    [KEY_F17] = GR_KEY_CODE_F17,
    [KEY_F18] = GR_KEY_CODE_F18,
    [KEY_F19] = GR_KEY_CODE_F19,
    [KEY_F20] = GR_KEY_CODE_F20,
    [KEY_F21] = GR_KEY_CODE_F21,
    [KEY_F22] = GR_KEY_CODE_F22,
    [KEY_F23] = GR_KEY_CODE_F23,
    [KEY_F24] = GR_KEY_CODE_F24,
    [KEY_HANGEUL] = GR_KEY_CODE_LANG_1,
    [KEY_HANJA] = GR_KEY_CODE_LANG_2,
    [KEY_KATAKANAHIRAGANA] = GR_KEY_CODE_KANA_MODE,
    [KEY_HENKAN] = GR_KEY_CODE_CONVERT,
    [KEY_MUHENKAN] = GR_KEY_CODE_NON_CONVERT,
    [KEY_MUTE] = GR_KEY_CODE_AUDIO_VOLUME_MUTE,
    [KEY_VOLUMEDOWN] = GR_KEY_CODE_AUDIO_VOLUME_DOWN,
    [KEY_VOLUMEUP] = GR_KEY_CODE_AUDIO_VOLUME_UP,
};
#endif

gr_key_code gr_gtk_key_code(guint keycode)
{
#ifdef HAVE_EVDEV
    if (keycode >= 8 && keycode - 8 < G_N_ELEMENTS(codes))
        return codes[keycode - 8];
#else
    (void)keycode;
#endif
    return GR_KEY_CODE_UNKNOWN;
}

gr_key gr_gtk_named_key(guint keyval)
{
    switch (keyval)
    {
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
        case GDK_KEY_ISO_Enter:
            return GR_KEY_ENTER;
        case GDK_KEY_Tab:
        case GDK_KEY_KP_Tab:
        case GDK_KEY_ISO_Left_Tab:
            return GR_KEY_TAB;
        case GDK_KEY_BackSpace:
            return GR_KEY_BACKSPACE;
        case GDK_KEY_Escape:
            return GR_KEY_ESCAPE;
        case GDK_KEY_Delete:
        case GDK_KEY_KP_Delete:
            return GR_KEY_DELETE;
        case GDK_KEY_Insert:
        case GDK_KEY_KP_Insert:
            return GR_KEY_INSERT;
        case GDK_KEY_Home:
        case GDK_KEY_KP_Home:
            return GR_KEY_HOME;
        case GDK_KEY_End:
        case GDK_KEY_KP_End:
            return GR_KEY_END;
        case GDK_KEY_Page_Up:
        case GDK_KEY_KP_Page_Up:
            return GR_KEY_PAGE_UP;
        case GDK_KEY_Page_Down:
        case GDK_KEY_KP_Page_Down:
            return GR_KEY_PAGE_DOWN;
        case GDK_KEY_Left:
        case GDK_KEY_KP_Left:
            return GR_KEY_ARROW_LEFT;
        case GDK_KEY_Right:
        case GDK_KEY_KP_Right:
            return GR_KEY_ARROW_RIGHT;
        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
            return GR_KEY_ARROW_UP;
        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
            return GR_KEY_ARROW_DOWN;
        case GDK_KEY_Shift_L:
        case GDK_KEY_Shift_R:
            return GR_KEY_SHIFT;
        case GDK_KEY_Control_L:
        case GDK_KEY_Control_R:
            return GR_KEY_CONTROL;
        case GDK_KEY_Alt_L:
        case GDK_KEY_Alt_R:
        case GDK_KEY_ISO_Level3_Shift:
            return GR_KEY_ALT;
        case GDK_KEY_Super_L:
        case GDK_KEY_Super_R:
        case GDK_KEY_Meta_L:
        case GDK_KEY_Meta_R:
        case GDK_KEY_Hyper_L:
        case GDK_KEY_Hyper_R:
            return GR_KEY_SUPER;
        case GDK_KEY_Caps_Lock:
            return GR_KEY_CAPS_LOCK;
        case GDK_KEY_Num_Lock:
            return GR_KEY_NUM_LOCK;
        case GDK_KEY_Scroll_Lock:
            return GR_KEY_SCROLL_LOCK;
        case GDK_KEY_Menu:
            return GR_KEY_CONTEXT_MENU;
        case GDK_KEY_Print:
            return GR_KEY_PRINT_SCREEN;
        case GDK_KEY_Pause:
            return GR_KEY_PAUSE;
        default:
            /* GDK_KEY_F1 to GDK_KEY_F24 are consecutive. */
            if (keyval >= GDK_KEY_F1 && keyval <= GDK_KEY_F24)
                return (gr_key)(GR_KEY_F1 + (keyval - GDK_KEY_F1));
            return GR_KEY_UNKNOWN;
    }
}
