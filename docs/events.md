# Events

Interactive scene hosts deliver input and frame events to the application
through one callback. This document describes the design they share. The
headers are the API reference ([event.h](../include/graphite/event.h),
[host.h](../include/graphite/host.h), and the host vtable in
[backend.h](../include/graphite/backend.h)); each platform's README and
backend notes describe how it maps native input.

## Scope

Graphite normalizes native events into one set of C structures in host
coordinates and paces frames to the display. It does not hit test, route
events through the layer tree, recognize gestures or manage a focus chain:
those stay with the application or toolkit, as
[the architecture](architecture.md) puts it.

The event API lives on `gr_scene_host`, the handle every platform attachment
returns. Application code only sees `gr_scene_host *`; platform code reports
native events with `gr_scene_host_dispatch_event` and implements the optional
event callbacks of `gr_scene_host_vtable`, the same layering as `gr_scene` and
its backend vtable.

## Interactive hosts

A host is interactive when it has a native input surface. Its vtable then sets
every event callback (`request_frame`, `now`, `set_cursor`, `set_text_input`,
`request_focus`); a partial set makes `gr_scene_host_new` fail.

| Host | Interactive |
|---|---|
| `gr_macos_attach_to_view` | Yes: a layer-hosting view that becomes first responder |
| `gr_gtk_attach` | Yes: a focusable widget with event controllers |
| `gr_windows_attach_to_hwnd` | Yes: a child window and its window procedure |
| `gr_web_attach_to_element` | Yes: a focusable element with DOM listeners |
| `gr_macos_attach_to_layer`, `gr_windows_attach_to_visual` | No |
| Offscreen hosts | No |

On hosts that are not interactive, the event functions return
`GR_ERROR_UNSUPPORTED`, false or NAN. An application that embeds a Scene in a
layer or visual and receives input itself can still use the event model: it
creates its own host with `gr_scene_host_new` and a vtable whose callbacks
forward to its own surface, then dispatches events into it. Tests do the same
with a vtable that records requests.

## Delivery

- Events arrive on the Scene's owning thread, synchronously from the native
  event loop, never inside a commit, an apply or a draw. Handlers may change
  the Scene and call host functions.
- The handler returns whether it consumed the event. Unconsumed events go back
  to the platform: the parent view, the responder chain, the input method or
  the default window procedure. Embedded in a larger application, shortcuts
  and scroll chaining keep working.
- Events dispatched while a handler runs (such as the focus changes a platform
  sends synchronously when the handler requests focus) are copied, delivered
  in order once it returns, and report false.
- Core holds a host reference during delivery, so a handler may release the
  host. Replacing the handler during delivery destroys the old one after the
  outermost delivery returns.
- Core records viewport and focus state, even without a handler, and drops
  reports that repeat it, so platforms report state liberally.
  `gr_scene_host_get_viewport` and `gr_scene_host_has_focus` return it.
- Builds without `NDEBUG` validate dispatched events (known enums, finite
  values, buttons on presses) and drop invalid ones; release builds trust the
  platform.

## Coordinates and time

Positions are in host space: logical points from the host's top-left corner,
y down, which is the Scene root's box space. Times are seconds on a monotonic
clock, meaningful only as differences. Frame times use the frame clock that
`gr_scene_host_get_time` reads. Input times use the platform's input clock,
which can differ from it (GTK reports compositor milliseconds), so compute
velocities from input times alone.

## Event types

| Type | Payload | Meaning |
|---|---|---|
| `FRAME` | target time, interval | A requested display refresh |
| `VIEWPORT` | size, scale | Host size or backing scale changed; the scale is already applied to the Scene, and the root's size is the application's choice |
| `FOCUS_IN`, `FOCUS_OUT` | none | Keyboard focus |
| `POINTER_DOWN`, `UP`, `MOVE` | pointer | Mouse and pen, in the style of W3C Pointer Events |
| `POINTER_ENTER`, `LEAVE` | pointer | Hovering into or out of the host |
| `POINTER_CANCEL` | pointer | The platform took the pointer away; treat it as an up without activation |
| `SCROLL` | scroll | Wheels and touchpads |
| `KEY_DOWN`, `KEY_UP` | key | Physical and logical keys |
| `TEXT_EDIT` | edit | Text input changed the shared editing state |
| `TEXT_COMMAND` | command | An editing command from the platform's key bindings |

Every input event carries `modifiers`, the state after the event: pressing
Shift reports `SHIFT`.

## Frames

`gr_scene_host_request_frame` asks for one `FRAME` at the next display
refresh. Requests coalesce until delivery; a handler that keeps animating
requests again from its `FRAME`. Without a request, hosts do not wake for
frames. Core clears the request before delivery and drops frames nobody
requested, so a platform may forward spare ticks.

The invariant that matters: a frame is delivered before its frame's commit
point, so what the handler changes is presented with that frame.

```
FRAME → the handler changes the Scene → commit point → the compositor presents
```

Each frame stands for one display refresh: `time` is the frame's start,
`target_time` the expected presentation, and `interval` the current refresh
interval.

## Pointer

- `id` is 0 for the mouse and stable from down to up for pen contacts; `kind`
  says mouse or pen. Touchscreens are not reported.
- `button` is the button that changed (DOWN and UP only), `buttons` the set
  still pressed after the event, and `click_count` follows the platform's
  double-click time and distance.
- `pressure` is in [0, 1]: the pen's own pressure, else 0.5 while pressed and 0
  while hovering, as on the Web.
- A press captures the pointer: moves and the release reach the host until
  every button is up, even outside it. Crossings are not reported during a
  drag; the one that happened is reported after the last release. A lost
  capture reports `POINTER_CANCEL`.
- A press gives the host keyboard focus.

## Scroll

- `delta` follows the Web: positive y scrolls down (content moves up), with
  the system's scroll direction applied.
- `unit` is `POINT` for precise deltas (touchpads, precise wheels) or `LINE`
  for wheel detents, possibly fractional; the application picks the line
  height.
- `phase` is `BEGIN`, `UPDATE` and `END` for touchpad gestures where the
  platform reports them, and `NONE` otherwise; `momentum` marks inertial
  scrolling the platform generates after the fingers lift.

## Keys

- `code` is the physical key, named after the W3C UI Events `code` values (the
  US layout key at that position). Each platform maps its native codes.
- `key` is the logical named key on the current layout; keys that type text
  are `GR_KEY_CHARACTER`, and keys an input method takes before the host sees
  them are `GR_KEY_PROCESS`.
- `character` is the code point the key types on the current layout without
  modifiers, lowercased. Match shortcuts against it, so Command+C works on
  AZERTY and Dvorak.
- `repeat` marks auto-repeat; `native_code` is the platform's own key code.

## Text input

Text input follows the model of `NSTextInputClient` and TSF, which also maps
onto GTK's preedit and the Web's text fields: the application and the platform
share one editing state, and the platform reports changes to it.

- A focused text field turns text input on with `gr_scene_host_set_text_input`
  and its state: kind (text or password), UTF-8 text, selection, composition,
  and either a text layout with its host-space origin or a caret rectangle.
  Input methods place their windows from the layout's characters and map
  points to offsets with it. The field turns text input off with NULL when it
  loses focus.
- The platform reports `TEXT_EDIT`: replace a range of the shared text with
  some text, then take a new selection and composition. Typed characters,
  compositions, accent menus, dictation and paste all arrive this way. Its
  key bindings report `TEXT_COMMAND`, an editing command (delete a word, move
  to the line's end, select all) with whether it extends the selection, so
  each platform's conventions hold.
- The platform keeps its own copy of the state and applies each of its edits
  to it before delivery, so input methods see consistent text before the
  application updates. The field applies the same edit to its own text and
  shares the result; it shares again whenever its text, selection, composition
  or layout change for another reason. A field that ends or replaces a
  composition itself, on a click elsewhere for example, makes the platform
  drop the input method's.
- Keys go to the handler first. A `KEY_DOWN` it leaves goes to text input,
  which types it, composes with it or turns it into a command; a consumed one
  types nothing. While a composition is in progress, keys belong to the input
  method: they are reported with `composing` set and the handler's result is
  ignored. On some platforms the input method sees keys before the host does
  (Windows, and GNOME on Wayland); those keys arrive as `GR_KEY_PROCESS` or
  not at all.
- Password fields hide their text from input methods, limit them to direct
  typing and secure input where the platform can.
- Core passes the application's state and the platform's events through.
  Keeping the copy, applying edits, offset conversion and geometry queries are
  platform work, since each platform needs a different part of it.

| Platform | Text input through |
|---|---|
| macOS | `NSTextInputClient` on the host view |
| GTK | A `GtkIMMulticontext` on the widget |
| Windows | IMM32 on the host window |
| Web | A hidden text field at the caret |

## Cursor and focus

`gr_scene_host_set_cursor` sets the cursor shown over the host, from a fixed
set (default, pointer, text, crosshair, grab, grabbing, four resize
directions, not allowed, none). `gr_scene_host_request_focus` asks the
platform for keyboard focus; `FOCUS_IN` follows if it gives it.

## Lifetime

- The host owns its handler. Final host release destroys the handler, then the
  platform host.
- A host holds a Scene link, not the Scene. Events keep arriving after the
  Scene is gone; the handler gets the Scene from `gr_scene_host_get_scene`,
  which then returns NULL.
- Native objects may outlive the host handle when their parent keeps them;
  after release they report nothing and only do the native default. Each
  platform documents what stays: the macOS view and the GTK widget stay where
  they are, the Windows child window is destroyed, and the Web element stays
  until the Scene goes.

## Example

```c
static bool on_event(void *userdata, const gr_event *e)
{
    app *a = userdata;
    switch (e->type)
    {
        case GR_EVENT_FRAME:
            app_animate(a, e->value.frame.target_time);
            if (app_is_animating(a))
                gr_scene_host_request_frame(a->host);
            return true;
        case GR_EVENT_POINTER_DOWN:
            return app_press(a, e->value.pointer.position); /* The application hit tests. */
        case GR_EVENT_KEY_DOWN:
            if ((e->modifiers & GR_MODIFIER_SUPER) && e->value.key.character == 'z')
                return app_undo(a);
            return false; /* Leave the key to text input and the platform. */
        case GR_EVENT_TEXT_EDIT:
            /* Apply the edit to the field, then share its new state with gr_scene_host_set_text_input. */
            return field_edit(a->field, &e->value.edit);
        case GR_EVENT_TEXT_COMMAND:
            return field_command(a->field, &e->value.command);
        default:
            return false;
    }
}

gr_scene_host *host = gr_macos_attach_to_view(scene, view); /* Or gr_gtk_attach, gr_windows_attach_to_hwnd, ... */
gr_scene_host_set_event_handler(host, &(gr_event_handler){.userdata = a, .handle = on_event});
gr_scene_host_request_frame(host);
```

`examples/input` shows every event, and its text field
(`examples/input/field.c`) is a complete text input client.

## Not covered

- Touchscreens, and gestures such as pinch and rotate.
- The history of coalesced pointer moves.
- Hit testing helpers and coordinate conversion between layers.
- Frames for hosts without a native surface: a layer or visual host has no
  view to take a display clock from, so the application paces those itself.
