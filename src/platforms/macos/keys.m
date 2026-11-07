#include "internal.h"

#include <Carbon/Carbon.h>
#include <IOKit/hidsystem/IOLLEvent.h>

typedef struct
{
    gr_key_code code;
    gr_key key; /* Named keys sit at fixed positions on Mac keyboards; others are resolved from their text. */
} key_entry;

/* Indexed by kVK_* virtual key code. */
static const key_entry keys[128] = {
    [kVK_ANSI_A] = {GR_KEY_CODE_A, GR_KEY_UNKNOWN},
    [kVK_ANSI_B] = {GR_KEY_CODE_B, GR_KEY_UNKNOWN},
    [kVK_ANSI_C] = {GR_KEY_CODE_C, GR_KEY_UNKNOWN},
    [kVK_ANSI_D] = {GR_KEY_CODE_D, GR_KEY_UNKNOWN},
    [kVK_ANSI_E] = {GR_KEY_CODE_E, GR_KEY_UNKNOWN},
    [kVK_ANSI_F] = {GR_KEY_CODE_F, GR_KEY_UNKNOWN},
    [kVK_ANSI_G] = {GR_KEY_CODE_G, GR_KEY_UNKNOWN},
    [kVK_ANSI_H] = {GR_KEY_CODE_H, GR_KEY_UNKNOWN},
    [kVK_ANSI_I] = {GR_KEY_CODE_I, GR_KEY_UNKNOWN},
    [kVK_ANSI_J] = {GR_KEY_CODE_J, GR_KEY_UNKNOWN},
    [kVK_ANSI_K] = {GR_KEY_CODE_K, GR_KEY_UNKNOWN},
    [kVK_ANSI_L] = {GR_KEY_CODE_L, GR_KEY_UNKNOWN},
    [kVK_ANSI_M] = {GR_KEY_CODE_M, GR_KEY_UNKNOWN},
    [kVK_ANSI_N] = {GR_KEY_CODE_N, GR_KEY_UNKNOWN},
    [kVK_ANSI_O] = {GR_KEY_CODE_O, GR_KEY_UNKNOWN},
    [kVK_ANSI_P] = {GR_KEY_CODE_P, GR_KEY_UNKNOWN},
    [kVK_ANSI_Q] = {GR_KEY_CODE_Q, GR_KEY_UNKNOWN},
    [kVK_ANSI_R] = {GR_KEY_CODE_R, GR_KEY_UNKNOWN},
    [kVK_ANSI_S] = {GR_KEY_CODE_S, GR_KEY_UNKNOWN},
    [kVK_ANSI_T] = {GR_KEY_CODE_T, GR_KEY_UNKNOWN},
    [kVK_ANSI_U] = {GR_KEY_CODE_U, GR_KEY_UNKNOWN},
    [kVK_ANSI_V] = {GR_KEY_CODE_V, GR_KEY_UNKNOWN},
    [kVK_ANSI_W] = {GR_KEY_CODE_W, GR_KEY_UNKNOWN},
    [kVK_ANSI_X] = {GR_KEY_CODE_X, GR_KEY_UNKNOWN},
    [kVK_ANSI_Y] = {GR_KEY_CODE_Y, GR_KEY_UNKNOWN},
    [kVK_ANSI_Z] = {GR_KEY_CODE_Z, GR_KEY_UNKNOWN},
    [kVK_ANSI_0] = {GR_KEY_CODE_DIGIT_0, GR_KEY_UNKNOWN},
    [kVK_ANSI_1] = {GR_KEY_CODE_DIGIT_1, GR_KEY_UNKNOWN},
    [kVK_ANSI_2] = {GR_KEY_CODE_DIGIT_2, GR_KEY_UNKNOWN},
    [kVK_ANSI_3] = {GR_KEY_CODE_DIGIT_3, GR_KEY_UNKNOWN},
    [kVK_ANSI_4] = {GR_KEY_CODE_DIGIT_4, GR_KEY_UNKNOWN},
    [kVK_ANSI_5] = {GR_KEY_CODE_DIGIT_5, GR_KEY_UNKNOWN},
    [kVK_ANSI_6] = {GR_KEY_CODE_DIGIT_6, GR_KEY_UNKNOWN},
    [kVK_ANSI_7] = {GR_KEY_CODE_DIGIT_7, GR_KEY_UNKNOWN},
    [kVK_ANSI_8] = {GR_KEY_CODE_DIGIT_8, GR_KEY_UNKNOWN},
    [kVK_ANSI_9] = {GR_KEY_CODE_DIGIT_9, GR_KEY_UNKNOWN},
    [kVK_ANSI_Minus] = {GR_KEY_CODE_MINUS, GR_KEY_UNKNOWN},
    [kVK_ANSI_Equal] = {GR_KEY_CODE_EQUAL, GR_KEY_UNKNOWN},
    [kVK_ANSI_LeftBracket] = {GR_KEY_CODE_BRACKET_LEFT, GR_KEY_UNKNOWN},
    [kVK_ANSI_RightBracket] = {GR_KEY_CODE_BRACKET_RIGHT, GR_KEY_UNKNOWN},
    [kVK_ANSI_Backslash] = {GR_KEY_CODE_BACKSLASH, GR_KEY_UNKNOWN},
    [kVK_ANSI_Semicolon] = {GR_KEY_CODE_SEMICOLON, GR_KEY_UNKNOWN},
    [kVK_ANSI_Quote] = {GR_KEY_CODE_QUOTE, GR_KEY_UNKNOWN},
    [kVK_ANSI_Grave] = {GR_KEY_CODE_BACKQUOTE, GR_KEY_UNKNOWN},
    [kVK_ANSI_Comma] = {GR_KEY_CODE_COMMA, GR_KEY_UNKNOWN},
    [kVK_ANSI_Period] = {GR_KEY_CODE_PERIOD, GR_KEY_UNKNOWN},
    [kVK_ANSI_Slash] = {GR_KEY_CODE_SLASH, GR_KEY_UNKNOWN},
    [kVK_ISO_Section] = {GR_KEY_CODE_INTL_BACKSLASH, GR_KEY_UNKNOWN},
    [kVK_JIS_Underscore] = {GR_KEY_CODE_INTL_RO, GR_KEY_UNKNOWN},
    [kVK_JIS_Yen] = {GR_KEY_CODE_INTL_YEN, GR_KEY_UNKNOWN},
    [kVK_Return] = {GR_KEY_CODE_ENTER, GR_KEY_ENTER},
    [kVK_Escape] = {GR_KEY_CODE_ESCAPE, GR_KEY_ESCAPE},
    [kVK_Delete] = {GR_KEY_CODE_BACKSPACE, GR_KEY_BACKSPACE},
    [kVK_Tab] = {GR_KEY_CODE_TAB, GR_KEY_TAB},
    [kVK_Space] = {GR_KEY_CODE_SPACE, GR_KEY_UNKNOWN},
    [kVK_CapsLock] = {GR_KEY_CODE_CAPS_LOCK, GR_KEY_CAPS_LOCK},
    [kVK_Shift] = {GR_KEY_CODE_SHIFT_LEFT, GR_KEY_SHIFT},
    [kVK_RightShift] = {GR_KEY_CODE_SHIFT_RIGHT, GR_KEY_SHIFT},
    [kVK_Control] = {GR_KEY_CODE_CONTROL_LEFT, GR_KEY_CONTROL},
    [kVK_RightControl] = {GR_KEY_CODE_CONTROL_RIGHT, GR_KEY_CONTROL},
    [kVK_Option] = {GR_KEY_CODE_ALT_LEFT, GR_KEY_ALT},
    [kVK_RightOption] = {GR_KEY_CODE_ALT_RIGHT, GR_KEY_ALT},
    [kVK_Command] = {GR_KEY_CODE_META_LEFT, GR_KEY_SUPER},
    [kVK_RightCommand] = {GR_KEY_CODE_META_RIGHT, GR_KEY_SUPER},
    [kVK_ContextualMenu] = {GR_KEY_CODE_CONTEXT_MENU, GR_KEY_CONTEXT_MENU},
    [kVK_Function] = {GR_KEY_CODE_FN, GR_KEY_FN},
    [kVK_LeftArrow] = {GR_KEY_CODE_ARROW_LEFT, GR_KEY_ARROW_LEFT},
    [kVK_RightArrow] = {GR_KEY_CODE_ARROW_RIGHT, GR_KEY_ARROW_RIGHT},
    [kVK_UpArrow] = {GR_KEY_CODE_ARROW_UP, GR_KEY_ARROW_UP},
    [kVK_DownArrow] = {GR_KEY_CODE_ARROW_DOWN, GR_KEY_ARROW_DOWN},
    [kVK_Help] = {GR_KEY_CODE_INSERT, GR_KEY_INSERT},
    [kVK_ForwardDelete] = {GR_KEY_CODE_DELETE, GR_KEY_DELETE},
    [kVK_Home] = {GR_KEY_CODE_HOME, GR_KEY_HOME},
    [kVK_End] = {GR_KEY_CODE_END, GR_KEY_END},
    [kVK_PageUp] = {GR_KEY_CODE_PAGE_UP, GR_KEY_PAGE_UP},
    [kVK_PageDown] = {GR_KEY_CODE_PAGE_DOWN, GR_KEY_PAGE_DOWN},
    [kVK_ANSI_KeypadClear] = {GR_KEY_CODE_NUM_LOCK, GR_KEY_NUM_LOCK},
    [kVK_ANSI_Keypad0] = {GR_KEY_CODE_NUMPAD_0, GR_KEY_UNKNOWN},
    [kVK_ANSI_Keypad1] = {GR_KEY_CODE_NUMPAD_1, GR_KEY_UNKNOWN},
    [kVK_ANSI_Keypad2] = {GR_KEY_CODE_NUMPAD_2, GR_KEY_UNKNOWN},
    [kVK_ANSI_Keypad3] = {GR_KEY_CODE_NUMPAD_3, GR_KEY_UNKNOWN},
    [kVK_ANSI_Keypad4] = {GR_KEY_CODE_NUMPAD_4, GR_KEY_UNKNOWN},
    [kVK_ANSI_Keypad5] = {GR_KEY_CODE_NUMPAD_5, GR_KEY_UNKNOWN},
    [kVK_ANSI_Keypad6] = {GR_KEY_CODE_NUMPAD_6, GR_KEY_UNKNOWN},
    [kVK_ANSI_Keypad7] = {GR_KEY_CODE_NUMPAD_7, GR_KEY_UNKNOWN},
    [kVK_ANSI_Keypad8] = {GR_KEY_CODE_NUMPAD_8, GR_KEY_UNKNOWN},
    [kVK_ANSI_Keypad9] = {GR_KEY_CODE_NUMPAD_9, GR_KEY_UNKNOWN},
    [kVK_ANSI_KeypadPlus] = {GR_KEY_CODE_NUMPAD_ADD, GR_KEY_UNKNOWN},
    [kVK_ANSI_KeypadMinus] = {GR_KEY_CODE_NUMPAD_SUBTRACT, GR_KEY_UNKNOWN},
    [kVK_ANSI_KeypadMultiply] = {GR_KEY_CODE_NUMPAD_MULTIPLY, GR_KEY_UNKNOWN},
    [kVK_ANSI_KeypadDivide] = {GR_KEY_CODE_NUMPAD_DIVIDE, GR_KEY_UNKNOWN},
    [kVK_ANSI_KeypadDecimal] = {GR_KEY_CODE_NUMPAD_DECIMAL, GR_KEY_UNKNOWN},
    [kVK_JIS_KeypadComma] = {GR_KEY_CODE_NUMPAD_COMMA, GR_KEY_UNKNOWN},
    [kVK_ANSI_KeypadEquals] = {GR_KEY_CODE_NUMPAD_EQUAL, GR_KEY_UNKNOWN},
    [kVK_ANSI_KeypadEnter] = {GR_KEY_CODE_NUMPAD_ENTER, GR_KEY_ENTER},
    [kVK_F1] = {GR_KEY_CODE_F1, GR_KEY_F1},
    [kVK_F2] = {GR_KEY_CODE_F2, GR_KEY_F2},
    [kVK_F3] = {GR_KEY_CODE_F3, GR_KEY_F3},
    [kVK_F4] = {GR_KEY_CODE_F4, GR_KEY_F4},
    [kVK_F5] = {GR_KEY_CODE_F5, GR_KEY_F5},
    [kVK_F6] = {GR_KEY_CODE_F6, GR_KEY_F6},
    [kVK_F7] = {GR_KEY_CODE_F7, GR_KEY_F7},
    [kVK_F8] = {GR_KEY_CODE_F8, GR_KEY_F8},
    [kVK_F9] = {GR_KEY_CODE_F9, GR_KEY_F9},
    [kVK_F10] = {GR_KEY_CODE_F10, GR_KEY_F10},
    [kVK_F11] = {GR_KEY_CODE_F11, GR_KEY_F11},
    [kVK_F12] = {GR_KEY_CODE_F12, GR_KEY_F12},
    [kVK_F13] = {GR_KEY_CODE_F13, GR_KEY_F13},
    [kVK_F14] = {GR_KEY_CODE_F14, GR_KEY_F14},
    [kVK_F15] = {GR_KEY_CODE_F15, GR_KEY_F15},
    [kVK_F16] = {GR_KEY_CODE_F16, GR_KEY_F16},
    [kVK_F17] = {GR_KEY_CODE_F17, GR_KEY_F17},
    [kVK_F18] = {GR_KEY_CODE_F18, GR_KEY_F18},
    [kVK_F19] = {GR_KEY_CODE_F19, GR_KEY_F19},
    [kVK_F20] = {GR_KEY_CODE_F20, GR_KEY_F20},
    [kVK_JIS_Kana] = {GR_KEY_CODE_LANG_1, GR_KEY_UNKNOWN},
    [kVK_JIS_Eisu] = {GR_KEY_CODE_LANG_2, GR_KEY_UNKNOWN},
    [kVK_Mute] = {GR_KEY_CODE_AUDIO_VOLUME_MUTE, GR_KEY_UNKNOWN},
    [kVK_VolumeDown] = {GR_KEY_CODE_AUDIO_VOLUME_DOWN, GR_KEY_UNKNOWN},
    [kVK_VolumeUp] = {GR_KEY_CODE_AUDIO_VOLUME_UP, GR_KEY_UNKNOWN},
};

gr_key_code gr_macos_key_code(unsigned short code)
{
    return code < 128 ? keys[code].code : GR_KEY_CODE_UNKNOWN;
}

gr_key gr_macos_named_key(unsigned short code)
{
    return code < 128 ? keys[code].key : GR_KEY_UNKNOWN;
}

NSUInteger gr_macos_modifier_mask(unsigned short code)
{
    switch (code)
    {
        case kVK_Shift:
            return NX_DEVICELSHIFTKEYMASK;
        case kVK_RightShift:
            return NX_DEVICERSHIFTKEYMASK;
        case kVK_Control:
            return NX_DEVICELCTLKEYMASK;
        case kVK_RightControl:
            return NX_DEVICERCTLKEYMASK;
        case kVK_Option:
            return NX_DEVICELALTKEYMASK;
        case kVK_RightOption:
            return NX_DEVICERALTKEYMASK;
        case kVK_Command:
            return NX_DEVICELCMDKEYMASK;
        case kVK_RightCommand:
            return NX_DEVICERCMDKEYMASK;
        case kVK_Function:
            return NSEventModifierFlagFunction;
        default:
            return 0;
    }
}
