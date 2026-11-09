# GTK / GSK backend

`graphite::gtk` (`graphite_gtk`) implements GTK hosting and GSK rendering. It requires
GTK **4.22+**, including its development headers, and is enabled explicitly with
`-DGRAPHITE_GTK=ON`. All platform calls run on GTK's main thread after initialization.
No additional container dependency is introduced: GTK already supplies GLib.

## Implementation

- `convert.c`: colors, geometry, paths, stroke trimming, textures and affine
  decomposition into 2D translate/rotate/skew/scale GSK transforms.
- `draw.c`: DisplayList to native GSK nodes. Clips, masks and save/restore are
  stacked; global alpha applies per drawing operation. No Cairo raster fallback
  is introduced by the Graphite backend.
- `tree.c`: per-layer immutable node caching, ancestor invalidation, shared image
  textures, two-pass reparenting and retirement. Masks are mask nodes over a
  path fill or the gradient.
  Shadow blur and outset radii use a calibrated factor of 2σ.
- `host.c`: opaque host ownership and backend callbacks. Stored resolved snapshots
  retain their resources. Scene teardown clears the node registry before layer
  pointer leases expire; surviving hosts/widgets become empty.
- `widget.c`: GtkWidget subclass with natural root sizing and clipped output.
  Commits run in frame-clock **layout**, after tick callbacks and before paint.
  Surface `notify::scale` follows fractional scale. Realize/unrealize connects and
  disconnects clock/surface handlers; callbacks use a weak Scene link.
  Widget hosts are interactive: requested frames run in the frame clock's
  **update** phase, before the layout-phase commit, with times from the frame
  clock and its refresh info. Allocation and scale changes report
  `GR_EVENT_VIEWPORT` (first on allocation). The widget is focusable and reports
  focus while it is the focus widget of the active window. A legacy event
  controller turns raw GDK events into pointer (mouse and tablet tools), scroll
  and key events; a motion controller reports crossings. Event positions are
  surface-relative and converted to the widget; Wayland scroll events carry
  none, so they use the last pointer position. Click counts come from the GTK
  double-click settings, auto-repeat from the pressed-key set, and modifier
  state is adjusted to after the event (GDK reports it before). `keys.c` maps
  evdev codes (keycode − 8) and keyvals. Presses grab focus; unconsumed events
  propagate. Cursors use CSS cursor names. Touchscreen events are not
  reported yet.

  Text input goes through a `GtkIMMulticontext`. While it is on, the widget
  keeps a copy of the shared state, focuses the context while it has focus, and
  sets its cursor location from the shared layout (the composition's start
  while composing). Key events the handler leaves go to
  `gtk_im_context_filter_keypress`, then to a table of GtkText's editing
  bindings that produce `GR_EVENT_TEXT_COMMAND`. `commit` and `preedit-changed`
  become `GR_EVENT_TEXT_EDIT` events applied to the copy first;
  `retrieve-surrounding` and `delete-surrounding` see the text without the
  composition, which the input method holds itself (and nothing for passwords).
  When the application ends or replaces a composition, the context is reset and
  whatever the reset emits is ignored. Under GNOME on Wayland (text-input-v3),
  the compositor routes keys to the input method before the application, so
  compositions start without a `GR_EVENT_KEY_DOWN`.
- `text.c`: the Pango text system (`gr_gtk_text_system_new`); `draw.c` replays
  its layouts with `gtk_snapshot_append_layout`. See the
  [backend notes](../../../docs/backends/gtk.md#text).
- `offscreen.c`: reusable GL, Vulkan or Cairo renderers, explicit premultiplied
  RGBA8 download, row-stride handling, and renderer unrealization after readback.

Public API: `include/graphite/gtk.h`. Attach with `gr_gtk_attach`, place the borrowed
widget in a native hierarchy, then release the host when no longer needed. The
parented widget retains shared state; neither host nor widget retains the Scene.
For offscreen work, use `gr_gtk_attach_offscreen`, set backing scale and commit
explicitly, then call `gr_gtk_renderer_render` with a reusable renderer.

The root-node accessor returns an owned GSK node; unchanged committed content
returns a reference to the same cached node. Drawer output is regenerated only
when `(display_version, DrawInfo)` changes, and its DisplayList is then released.

## Build and run

```sh
cmake -S . -B /tmp/graphite-gtk -G Ninja -DGRAPHITE_GTK=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/graphite-gtk --target check
```

Use a graphical session. When running via SSH into the validation VM:

```sh
export XDG_RUNTIME_DIR=/run/user/1000
export WAYLAND_DISPLAY=wayland-0
export LIBGL_ALWAYS_SOFTWARE=1
export GRAPHITE_GSK_RENDERERS=gl,cairo
/tmp/graphite-gtk/src/platforms/gtk/graphite_gtk_tests
/tmp/graphite-gtk/conformance/graphite_gtk_conformance
```

The conformance selector accepts `gl`, `vulkan`, and `cairo` (comma separated),
with default `gl,vulkan`. Renderer initialization failure is an
error, not a silent skip. Output goes to `GRAPHITE_CONFORMANCE_OUT/<renderer>/`.
Use separate reference/candidate output directories when comparing captures.

## Tests and renderer caveats

`tests/gtk.c` covers real-window rendering/idle wakeup, natural size, fractional
scale, unparent/reparent, 60 frame-clock ticks, node/drawer reuse, empty output
and host/Scene lifetimes. Text is covered by the conformance suite's text cases.
Native tests require a live graphical session.

Virtual GPU drivers can produce stale captures or incorrect blur output. Use
software GL (`LIBGL_ALWAYS_SOFTWARE=1`) or lavapipe where needed; renderer
initialization failures are errors, not silent skips. Compare only the same
renderer configuration when evaluating captures.

Some Fontconfig/EGL allocations also appear in standalone GTK applications.
`tests/gtk-lsan.supp` optionally suppresses those stacks; Graphite and generic
GLib allocations remain checked. It is not enabled automatically:

```sh
cmake -S . -B /tmp/graphite-gtk-asan -DGRAPHITE_GTK=ON -DGRAPHITE_SANITIZERS=ON
LSAN_OPTIONS="suppressions=$PWD/tests/gtk-lsan.supp" GRAPHITE_GSK_RENDERERS=gl,cairo \
  cmake --build /tmp/graphite-gtk-asan --target check
```

See [example usage](../../../examples/README.md) for the frame-clock shell.
