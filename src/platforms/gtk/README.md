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
- `tree.c`: per-layer immutable node caching, ancestor invalidation through both
  child and mask edges, shared image textures, two-pass reparenting and retirement.
  Shadow blur and outset radii use a calibrated factor of 2σ.
- `host.c`: opaque host ownership and backend callbacks. Stored resolved snapshots
  retain their resources. Scene teardown clears the node registry before layer
  pointer leases expire; surviving hosts/widgets become empty.
- `widget.c`: GtkWidget subclass with natural root sizing and clipped output.
  Commits run in frame-clock **layout**, after tick callbacks and before paint.
  Surface `notify::scale` follows fractional scale. Realize/unrealize connects and
  disconnects clock/surface handlers; callbacks use a weak Scene link.
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
and host/Scene lifetimes. Native tests require a live graphical session.

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

See [demo usage](../../../examples/demo/README.md) for the frame-clock shell.
