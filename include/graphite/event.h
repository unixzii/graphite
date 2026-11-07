/** Graphite input and frame events delivered by interactive scene hosts (see host.h). */
#ifndef GRAPHITE_EVENT_H
#define GRAPHITE_EVENT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Events
 *
 * Positions are in host space: logical points from the host's top-left corner, the Scene root's box space. Times are
 * seconds on a monotonic clock; only differences are meaningful. Frame times use the gr_scene_host_get_time domain;
 * input times use the platform's input clock, which may differ (GTK). */

typedef enum
{
    GR_EVENT_FRAME,          /**< A requested display refresh; see gr_scene_host_request_frame. */
    GR_EVENT_VIEWPORT,       /**< Host size or backing scale changed. */
    GR_EVENT_FOCUS_IN,       /**< The host gained keyboard focus. */
    GR_EVENT_FOCUS_OUT,      /**< The host lost keyboard focus. */
    GR_EVENT_POINTER_DOWN,   /**< A button was pressed or a contact touched down. */
    GR_EVENT_POINTER_UP,     /**< A button was released or a contact lifted. */
    GR_EVENT_POINTER_MOVE,   /**< The pointer moved, including drags captured outside the host. */
    GR_EVENT_POINTER_ENTER,  /**< The pointer entered the host while hovering. */
    GR_EVENT_POINTER_LEAVE,  /**< The pointer left the host while hovering. */
    GR_EVENT_POINTER_CANCEL, /**< The platform took the pointer away; treat as up without activation. */
    GR_EVENT_SCROLL,         /**< Wheel or touchpad scrolling. */
    GR_EVENT_KEY_DOWN,       /**< A key was pressed or auto-repeated. */
    GR_EVENT_KEY_UP,         /**< A key was released. */
    GR_EVENT_TEXT_COMMIT,    /**< Text to insert, typed or committed by an input method. */
    GR_EVENT_TEXT_PREEDIT    /**< Input method composition text; empty text ends the composition. */
} gr_event_type;

typedef uint32_t gr_modifiers;

enum
{
    GR_MODIFIER_SHIFT = 1u,
    GR_MODIFIER_CONTROL = 2u,
    GR_MODIFIER_ALT = 4u,   /**< Option on macOS. */
    GR_MODIFIER_SUPER = 8u, /**< Command on macOS, the Windows/Super key elsewhere. */
    GR_MODIFIER_CAPS_LOCK = 16u
};

typedef struct
{
    double target_time; /**< Expected presentation time of what this frame commits. */
    double interval;    /**< Current refresh interval; nonnegative. */
} gr_frame_event;

typedef struct
{
    gr_size size; /**< Host size in logical points. */
    double scale; /**< Backing scale, already applied to the Scene. */
} gr_viewport_event;

typedef enum
{
    GR_POINTER_MOUSE,
    GR_POINTER_PEN,
    GR_POINTER_TOUCH
} gr_pointer_kind;

typedef enum
{
    GR_BUTTON_NONE,
    GR_BUTTON_PRIMARY,
    GR_BUTTON_SECONDARY,
    GR_BUTTON_MIDDLE,
    GR_BUTTON_BACK,
    GR_BUTTON_FORWARD
} gr_button;

typedef struct
{
    uint32_t id; /**< 0 for the mouse; stable from down to up for touch and pen contacts. */
    gr_pointer_kind kind;
    gr_point position;
    gr_button button;     /**< The changed button for DOWN/UP (never NONE there); NONE otherwise. */
    uint32_t buttons;     /**< Bit (1u << button) for each button still pressed after this event. */
    uint32_t click_count; /**< DOWN/UP: consecutive clicks by the platform double-click setting; 0 otherwise. */
    float pressure;       /**< [0,1]; 0.5 while pressed for devices without pressure, 0 while hovering. */
} gr_pointer_event;

typedef enum
{
    GR_SCROLL_UNIT_POINT, /**< Precise deltas in logical points (touchpads, precise wheels). */
    GR_SCROLL_UNIT_LINE   /**< Wheel detents; the application chooses the line height. */
} gr_scroll_unit;

typedef enum
{
    GR_SCROLL_PHASE_NONE, /**< The platform reports no phases (Windows, plain wheels). */
    GR_SCROLL_PHASE_BEGIN,
    GR_SCROLL_PHASE_UPDATE,
    GR_SCROLL_PHASE_END
} gr_scroll_phase;

typedef struct
{
    gr_point position;
    gr_point delta; /**< Positive y scrolls down (content moves up); the system scroll direction is applied. */
    gr_scroll_unit unit;
    gr_scroll_phase phase;
    bool momentum; /**< Inertial scrolling the platform generates after the fingers lift. */
} gr_scroll_event;

/** Physical key position, named after the W3C UI Events "code" values (a US layout key at that position). */
typedef enum
{
    GR_KEY_CODE_UNKNOWN,
    GR_KEY_CODE_A,
    GR_KEY_CODE_B,
    GR_KEY_CODE_C,
    GR_KEY_CODE_D,
    GR_KEY_CODE_E,
    GR_KEY_CODE_F,
    GR_KEY_CODE_G,
    GR_KEY_CODE_H,
    GR_KEY_CODE_I,
    GR_KEY_CODE_J,
    GR_KEY_CODE_K,
    GR_KEY_CODE_L,
    GR_KEY_CODE_M,
    GR_KEY_CODE_N,
    GR_KEY_CODE_O,
    GR_KEY_CODE_P,
    GR_KEY_CODE_Q,
    GR_KEY_CODE_R,
    GR_KEY_CODE_S,
    GR_KEY_CODE_T,
    GR_KEY_CODE_U,
    GR_KEY_CODE_V,
    GR_KEY_CODE_W,
    GR_KEY_CODE_X,
    GR_KEY_CODE_Y,
    GR_KEY_CODE_Z,
    GR_KEY_CODE_DIGIT_0,
    GR_KEY_CODE_DIGIT_1,
    GR_KEY_CODE_DIGIT_2,
    GR_KEY_CODE_DIGIT_3,
    GR_KEY_CODE_DIGIT_4,
    GR_KEY_CODE_DIGIT_5,
    GR_KEY_CODE_DIGIT_6,
    GR_KEY_CODE_DIGIT_7,
    GR_KEY_CODE_DIGIT_8,
    GR_KEY_CODE_DIGIT_9,
    GR_KEY_CODE_MINUS,
    GR_KEY_CODE_EQUAL,
    GR_KEY_CODE_BRACKET_LEFT,
    GR_KEY_CODE_BRACKET_RIGHT,
    GR_KEY_CODE_BACKSLASH,
    GR_KEY_CODE_SEMICOLON,
    GR_KEY_CODE_QUOTE,
    GR_KEY_CODE_BACKQUOTE,
    GR_KEY_CODE_COMMA,
    GR_KEY_CODE_PERIOD,
    GR_KEY_CODE_SLASH,
    GR_KEY_CODE_INTL_BACKSLASH,
    GR_KEY_CODE_INTL_RO,
    GR_KEY_CODE_INTL_YEN,
    GR_KEY_CODE_ENTER,
    GR_KEY_CODE_ESCAPE,
    GR_KEY_CODE_BACKSPACE,
    GR_KEY_CODE_TAB,
    GR_KEY_CODE_SPACE,
    GR_KEY_CODE_CAPS_LOCK,
    GR_KEY_CODE_SHIFT_LEFT,
    GR_KEY_CODE_SHIFT_RIGHT,
    GR_KEY_CODE_CONTROL_LEFT,
    GR_KEY_CODE_CONTROL_RIGHT,
    GR_KEY_CODE_ALT_LEFT,
    GR_KEY_CODE_ALT_RIGHT,
    GR_KEY_CODE_META_LEFT,
    GR_KEY_CODE_META_RIGHT,
    GR_KEY_CODE_CONTEXT_MENU,
    GR_KEY_CODE_FN,
    GR_KEY_CODE_ARROW_LEFT,
    GR_KEY_CODE_ARROW_RIGHT,
    GR_KEY_CODE_ARROW_UP,
    GR_KEY_CODE_ARROW_DOWN,
    GR_KEY_CODE_INSERT,
    GR_KEY_CODE_DELETE,
    GR_KEY_CODE_HOME,
    GR_KEY_CODE_END,
    GR_KEY_CODE_PAGE_UP,
    GR_KEY_CODE_PAGE_DOWN,
    GR_KEY_CODE_PRINT_SCREEN,
    GR_KEY_CODE_SCROLL_LOCK,
    GR_KEY_CODE_PAUSE,
    GR_KEY_CODE_NUM_LOCK,
    GR_KEY_CODE_NUMPAD_0,
    GR_KEY_CODE_NUMPAD_1,
    GR_KEY_CODE_NUMPAD_2,
    GR_KEY_CODE_NUMPAD_3,
    GR_KEY_CODE_NUMPAD_4,
    GR_KEY_CODE_NUMPAD_5,
    GR_KEY_CODE_NUMPAD_6,
    GR_KEY_CODE_NUMPAD_7,
    GR_KEY_CODE_NUMPAD_8,
    GR_KEY_CODE_NUMPAD_9,
    GR_KEY_CODE_NUMPAD_ADD,
    GR_KEY_CODE_NUMPAD_SUBTRACT,
    GR_KEY_CODE_NUMPAD_MULTIPLY,
    GR_KEY_CODE_NUMPAD_DIVIDE,
    GR_KEY_CODE_NUMPAD_DECIMAL,
    GR_KEY_CODE_NUMPAD_COMMA,
    GR_KEY_CODE_NUMPAD_EQUAL,
    GR_KEY_CODE_NUMPAD_ENTER,
    GR_KEY_CODE_F1,
    GR_KEY_CODE_F2,
    GR_KEY_CODE_F3,
    GR_KEY_CODE_F4,
    GR_KEY_CODE_F5,
    GR_KEY_CODE_F6,
    GR_KEY_CODE_F7,
    GR_KEY_CODE_F8,
    GR_KEY_CODE_F9,
    GR_KEY_CODE_F10,
    GR_KEY_CODE_F11,
    GR_KEY_CODE_F12,
    GR_KEY_CODE_F13,
    GR_KEY_CODE_F14,
    GR_KEY_CODE_F15,
    GR_KEY_CODE_F16,
    GR_KEY_CODE_F17,
    GR_KEY_CODE_F18,
    GR_KEY_CODE_F19,
    GR_KEY_CODE_F20,
    GR_KEY_CODE_F21,
    GR_KEY_CODE_F22,
    GR_KEY_CODE_F23,
    GR_KEY_CODE_F24,
    GR_KEY_CODE_LANG_1, /**< Kana/Hangul toggle; Eisu on Mac JIS keyboards is LANG_2. */
    GR_KEY_CODE_LANG_2,
    GR_KEY_CODE_KANA_MODE,
    GR_KEY_CODE_CONVERT,
    GR_KEY_CODE_NON_CONVERT,
    GR_KEY_CODE_AUDIO_VOLUME_MUTE,
    GR_KEY_CODE_AUDIO_VOLUME_DOWN,
    GR_KEY_CODE_AUDIO_VOLUME_UP
} gr_key_code;

/** Logical key on the current layout. Keys that produce text are GR_KEY_CHARACTER; see gr_key_event.character. */
typedef enum
{
    GR_KEY_UNKNOWN,
    GR_KEY_CHARACTER,
    GR_KEY_PROCESS, /**< Consumed by an input method; the logical key is not known. */
    GR_KEY_ENTER,
    GR_KEY_TAB,
    GR_KEY_BACKSPACE,
    GR_KEY_ESCAPE,
    GR_KEY_DELETE,
    GR_KEY_INSERT,
    GR_KEY_HOME,
    GR_KEY_END,
    GR_KEY_PAGE_UP,
    GR_KEY_PAGE_DOWN,
    GR_KEY_ARROW_LEFT,
    GR_KEY_ARROW_RIGHT,
    GR_KEY_ARROW_UP,
    GR_KEY_ARROW_DOWN,
    GR_KEY_SHIFT,
    GR_KEY_CONTROL,
    GR_KEY_ALT,
    GR_KEY_SUPER,
    GR_KEY_CAPS_LOCK,
    GR_KEY_NUM_LOCK,
    GR_KEY_SCROLL_LOCK,
    GR_KEY_FN,
    GR_KEY_CONTEXT_MENU,
    GR_KEY_PRINT_SCREEN,
    GR_KEY_PAUSE,
    GR_KEY_F1,
    GR_KEY_F2,
    GR_KEY_F3,
    GR_KEY_F4,
    GR_KEY_F5,
    GR_KEY_F6,
    GR_KEY_F7,
    GR_KEY_F8,
    GR_KEY_F9,
    GR_KEY_F10,
    GR_KEY_F11,
    GR_KEY_F12,
    GR_KEY_F13,
    GR_KEY_F14,
    GR_KEY_F15,
    GR_KEY_F16,
    GR_KEY_F17,
    GR_KEY_F18,
    GR_KEY_F19,
    GR_KEY_F20,
    GR_KEY_F21,
    GR_KEY_F22,
    GR_KEY_F23,
    GR_KEY_F24
} gr_key;

typedef struct
{
    gr_key_code code;
    gr_key key;
    uint32_t character;   /**< Code point the key types on the current layout without modifiers, lowercase; 0 if none.
                           * Match shortcuts against this. */
    bool repeat;          /**< KEY_DOWN generated by auto-repeat. */
    bool composing;       /**< An input method composition owns this key; the handled result is ignored. */
    uint32_t native_code; /**< macOS kVK_* code, X11/evdev keycode, or Windows scan code with the extended bit. */
} gr_key_event;

typedef struct
{
    const char *text; /**< Valid UTF-8, borrowed for the callback, not NUL-terminated; NULL only when length is 0. */
    size_t length;    /**< In bytes. */
    size_t selection_start, selection_end; /**< PREEDIT: caret or selection in UTF-8 bytes; 0 for COMMIT. */
} gr_text_event;

/** One event. value holds the member named after the type's group; FOCUS events have no payload. */
typedef struct
{
    gr_event_type type;
    double time;            /**< Event time; for FRAME, the start of the frame. */
    gr_modifiers modifiers; /**< Modifiers held during input events; 0 for FRAME, VIEWPORT and FOCUS events. */

    union
    {
        gr_frame_event frame;
        gr_viewport_event viewport;
        gr_pointer_event pointer; /**< All POINTER_* events. */
        gr_scroll_event scroll;
        gr_key_event key;   /**< KEY_DOWN, KEY_UP. */
        gr_text_event text; /**< TEXT_COMMIT, TEXT_PREEDIT. */
    } value;
} gr_event;

/** Event callback. Ownership follows gr_drawer: userdata transfers on GR_OK and destroy runs exactly once.
 * handle runs on the Scene's owning thread from the native event loop, never inside a commit or a draw, so it may
 * mutate the Scene and call host functions. Return true when the event was consumed; false lets the host pass it on
 * natively (to the parent view, the input method or the default window procedure). Event storage is borrowed for the
 * call. destroy must not use the host. */
typedef struct
{
    void *userdata;
    bool (*handle)(void *userdata, const gr_event *event);
    void (*destroy)(void *userdata); /**< Optional. */
} gr_event_handler;

typedef enum
{
    GR_CURSOR_DEFAULT,
    GR_CURSOR_POINTER, /**< A link or clickable control. */
    GR_CURSOR_TEXT,
    GR_CURSOR_CROSSHAIR,
    GR_CURSOR_GRAB,
    GR_CURSOR_GRABBING,
    GR_CURSOR_RESIZE_EW,
    GR_CURSOR_RESIZE_NS,
    GR_CURSOR_RESIZE_NESW,
    GR_CURSOR_RESIZE_NWSE,
    GR_CURSOR_NOT_ALLOWED,
    GR_CURSOR_NONE /**< Hidden while over the host. */
} gr_cursor;

typedef struct
{
    gr_rect caret; /**< Host space; places the input method's candidate window. */
} gr_text_input;

#ifdef __cplusplus
}
#endif

#endif
