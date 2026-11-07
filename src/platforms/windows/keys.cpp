#include "internal.hpp"

namespace gr_win
{
namespace
{
struct KeyTables
{
    gr_key_code plain[128]{};    /* Indexed by scan code (set 1). */
    gr_key_code extended[128]{}; /* Indexed by scan code after an E0 prefix. */

    KeyTables()
    {
        static constexpr struct
        {
            unsigned scan;
            gr_key_code code;
        } entries[] = {
            {0x01, GR_KEY_CODE_ESCAPE},
            {0x02, GR_KEY_CODE_DIGIT_1},
            {0x03, GR_KEY_CODE_DIGIT_2},
            {0x04, GR_KEY_CODE_DIGIT_3},
            {0x05, GR_KEY_CODE_DIGIT_4},
            {0x06, GR_KEY_CODE_DIGIT_5},
            {0x07, GR_KEY_CODE_DIGIT_6},
            {0x08, GR_KEY_CODE_DIGIT_7},
            {0x09, GR_KEY_CODE_DIGIT_8},
            {0x0A, GR_KEY_CODE_DIGIT_9},
            {0x0B, GR_KEY_CODE_DIGIT_0},
            {0x0C, GR_KEY_CODE_MINUS},
            {0x0D, GR_KEY_CODE_EQUAL},
            {0x0E, GR_KEY_CODE_BACKSPACE},
            {0x0F, GR_KEY_CODE_TAB},
            {0x10, GR_KEY_CODE_Q},
            {0x11, GR_KEY_CODE_W},
            {0x12, GR_KEY_CODE_E},
            {0x13, GR_KEY_CODE_R},
            {0x14, GR_KEY_CODE_T},
            {0x15, GR_KEY_CODE_Y},
            {0x16, GR_KEY_CODE_U},
            {0x17, GR_KEY_CODE_I},
            {0x18, GR_KEY_CODE_O},
            {0x19, GR_KEY_CODE_P},
            {0x1A, GR_KEY_CODE_BRACKET_LEFT},
            {0x1B, GR_KEY_CODE_BRACKET_RIGHT},
            {0x1C, GR_KEY_CODE_ENTER},
            {0x1D, GR_KEY_CODE_CONTROL_LEFT},
            {0x1E, GR_KEY_CODE_A},
            {0x1F, GR_KEY_CODE_S},
            {0x20, GR_KEY_CODE_D},
            {0x21, GR_KEY_CODE_F},
            {0x22, GR_KEY_CODE_G},
            {0x23, GR_KEY_CODE_H},
            {0x24, GR_KEY_CODE_J},
            {0x25, GR_KEY_CODE_K},
            {0x26, GR_KEY_CODE_L},
            {0x27, GR_KEY_CODE_SEMICOLON},
            {0x28, GR_KEY_CODE_QUOTE},
            {0x29, GR_KEY_CODE_BACKQUOTE},
            {0x2A, GR_KEY_CODE_SHIFT_LEFT},
            {0x2B, GR_KEY_CODE_BACKSLASH},
            {0x2C, GR_KEY_CODE_Z},
            {0x2D, GR_KEY_CODE_X},
            {0x2E, GR_KEY_CODE_C},
            {0x2F, GR_KEY_CODE_V},
            {0x30, GR_KEY_CODE_B},
            {0x31, GR_KEY_CODE_N},
            {0x32, GR_KEY_CODE_M},
            {0x33, GR_KEY_CODE_COMMA},
            {0x34, GR_KEY_CODE_PERIOD},
            {0x35, GR_KEY_CODE_SLASH},
            {0x36, GR_KEY_CODE_SHIFT_RIGHT},
            {0x37, GR_KEY_CODE_NUMPAD_MULTIPLY},
            {0x38, GR_KEY_CODE_ALT_LEFT},
            {0x39, GR_KEY_CODE_SPACE},
            {0x3A, GR_KEY_CODE_CAPS_LOCK},
            {0x3B, GR_KEY_CODE_F1},
            {0x3C, GR_KEY_CODE_F2},
            {0x3D, GR_KEY_CODE_F3},
            {0x3E, GR_KEY_CODE_F4},
            {0x3F, GR_KEY_CODE_F5},
            {0x40, GR_KEY_CODE_F6},
            {0x41, GR_KEY_CODE_F7},
            {0x42, GR_KEY_CODE_F8},
            {0x43, GR_KEY_CODE_F9},
            {0x44, GR_KEY_CODE_F10},
            {0x45, GR_KEY_CODE_PAUSE}, /* Windows reports Pause without E0 and Num Lock with it. */
            {0x46, GR_KEY_CODE_SCROLL_LOCK},
            {0x47, GR_KEY_CODE_NUMPAD_7},
            {0x48, GR_KEY_CODE_NUMPAD_8},
            {0x49, GR_KEY_CODE_NUMPAD_9},
            {0x4A, GR_KEY_CODE_NUMPAD_SUBTRACT},
            {0x4B, GR_KEY_CODE_NUMPAD_4},
            {0x4C, GR_KEY_CODE_NUMPAD_5},
            {0x4D, GR_KEY_CODE_NUMPAD_6},
            {0x4E, GR_KEY_CODE_NUMPAD_ADD},
            {0x4F, GR_KEY_CODE_NUMPAD_1},
            {0x50, GR_KEY_CODE_NUMPAD_2},
            {0x51, GR_KEY_CODE_NUMPAD_3},
            {0x52, GR_KEY_CODE_NUMPAD_0},
            {0x53, GR_KEY_CODE_NUMPAD_DECIMAL},
            {0x54, GR_KEY_CODE_PRINT_SCREEN}, /* SysRq: Alt+Print Screen. */
            {0x56, GR_KEY_CODE_INTL_BACKSLASH},
            {0x57, GR_KEY_CODE_F11},
            {0x58, GR_KEY_CODE_F12},
            {0x59, GR_KEY_CODE_NUMPAD_EQUAL},
            {0x64, GR_KEY_CODE_F13},
            {0x65, GR_KEY_CODE_F14},
            {0x66, GR_KEY_CODE_F15},
            {0x67, GR_KEY_CODE_F16},
            {0x68, GR_KEY_CODE_F17},
            {0x69, GR_KEY_CODE_F18},
            {0x6A, GR_KEY_CODE_F19},
            {0x6B, GR_KEY_CODE_F20},
            {0x6C, GR_KEY_CODE_F21},
            {0x6D, GR_KEY_CODE_F22},
            {0x6E, GR_KEY_CODE_F23},
            {0x70, GR_KEY_CODE_KANA_MODE},
            {0x71, GR_KEY_CODE_LANG_2},
            {0x72, GR_KEY_CODE_LANG_1},
            {0x73, GR_KEY_CODE_INTL_RO},
            {0x76, GR_KEY_CODE_F24},
            {0x79, GR_KEY_CODE_CONVERT},
            {0x7B, GR_KEY_CODE_NON_CONVERT},
            {0x7D, GR_KEY_CODE_INTL_YEN},
            {0x7E, GR_KEY_CODE_NUMPAD_COMMA},
            {0xE01C, GR_KEY_CODE_NUMPAD_ENTER},
            {0xE01D, GR_KEY_CODE_CONTROL_RIGHT},
            {0xE020, GR_KEY_CODE_AUDIO_VOLUME_MUTE},
            {0xE02E, GR_KEY_CODE_AUDIO_VOLUME_DOWN},
            {0xE030, GR_KEY_CODE_AUDIO_VOLUME_UP},
            {0xE035, GR_KEY_CODE_NUMPAD_DIVIDE},
            {0xE037, GR_KEY_CODE_PRINT_SCREEN},
            {0xE038, GR_KEY_CODE_ALT_RIGHT},
            {0xE045, GR_KEY_CODE_NUM_LOCK},
            {0xE047, GR_KEY_CODE_HOME},
            {0xE048, GR_KEY_CODE_ARROW_UP},
            {0xE049, GR_KEY_CODE_PAGE_UP},
            {0xE04B, GR_KEY_CODE_ARROW_LEFT},
            {0xE04D, GR_KEY_CODE_ARROW_RIGHT},
            {0xE04F, GR_KEY_CODE_END},
            {0xE050, GR_KEY_CODE_ARROW_DOWN},
            {0xE051, GR_KEY_CODE_PAGE_DOWN},
            {0xE052, GR_KEY_CODE_INSERT},
            {0xE053, GR_KEY_CODE_DELETE},
            {0xE05B, GR_KEY_CODE_META_LEFT},
            {0xE05C, GR_KEY_CODE_META_RIGHT},
            {0xE05D, GR_KEY_CODE_CONTEXT_MENU},
        };

        for (auto const &e : entries)
            (e.scan & 0xE000 ? extended : plain)[e.scan & 0x7F] = e.code;
    }
};
} // namespace

gr_key_code key_code(unsigned scan)
{
    static const KeyTables tables;
    unsigned index = scan & 0xFF;
    if (index >= 128)
        return GR_KEY_CODE_UNKNOWN;
    return (scan & 0xE000 ? tables.extended : tables.plain)[index];
}

gr_key named_key(unsigned vk)
{
    switch (vk)
    {
        case VK_RETURN:
            return GR_KEY_ENTER;
        case VK_TAB:
            return GR_KEY_TAB;
        case VK_BACK:
            return GR_KEY_BACKSPACE;
        case VK_ESCAPE:
            return GR_KEY_ESCAPE;
        case VK_DELETE:
            return GR_KEY_DELETE;
        case VK_INSERT:
            return GR_KEY_INSERT;
        case VK_HOME:
            return GR_KEY_HOME;
        case VK_END:
            return GR_KEY_END;
        case VK_PRIOR:
            return GR_KEY_PAGE_UP;
        case VK_NEXT:
            return GR_KEY_PAGE_DOWN;
        case VK_LEFT:
            return GR_KEY_ARROW_LEFT;
        case VK_RIGHT:
            return GR_KEY_ARROW_RIGHT;
        case VK_UP:
            return GR_KEY_ARROW_UP;
        case VK_DOWN:
            return GR_KEY_ARROW_DOWN;
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            return GR_KEY_SHIFT;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            return GR_KEY_CONTROL;
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            return GR_KEY_ALT;
        case VK_LWIN:
        case VK_RWIN:
            return GR_KEY_SUPER;
        case VK_CAPITAL:
            return GR_KEY_CAPS_LOCK;
        case VK_NUMLOCK:
            return GR_KEY_NUM_LOCK;
        case VK_SCROLL:
            return GR_KEY_SCROLL_LOCK;
        case VK_APPS:
            return GR_KEY_CONTEXT_MENU;
        case VK_SNAPSHOT:
            return GR_KEY_PRINT_SCREEN;
        case VK_PAUSE:
            return GR_KEY_PAUSE;
        case VK_PROCESSKEY:
            return GR_KEY_PROCESS;
        default:
            if (vk >= VK_F1 && vk <= VK_F24)
                return static_cast<gr_key>(GR_KEY_F1 + (vk - VK_F1));
            return GR_KEY_UNKNOWN;
    }
}
} // namespace gr_win
