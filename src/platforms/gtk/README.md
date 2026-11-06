# GTK / GSK backend

`graphite::gtk` (`graphite_gtk`) ports `crates/graphite-gtk` to C. It requires
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
  Shadow blur and outset radii retain the Rust backend's calibrated factor of 2σ.
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
matching Rust; its default is `gl,vulkan`. Renderer initialization failure is an
error, not a silent skip. Output goes to `GRAPHITE_CONFORMANCE_OUT/<renderer>/`.
Use the existing comparison tool with separate C and Rust output directories.

## Validation

Validated on `cyan@192.168.64.9`: Ubuntu 26.04.1 arm64, GTK 4.22.4, GCC 15.2,
Wayland. Source/build directories are `/home/cyan/graphite-c-migration` and
`/home/cyan/graphite-c-{build,release,asan}`. Rust source remains unchanged.

- Debug and Release full checks passed, including format, core suites, native
  GTK tests, conformance helper/comparator tests and 99 renderer/case combinations.
- GL (llvmpipe) and Cairo: 111 captures each exactly match Rust.
- Vulkan with `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json` (lavapipe):
  another 111 captures match, for **333/333 images and 117 measurement-log groups**.
- The VM's default Vulkan driver produced two sporadic dirty-pixel captures in
  the initial Rust baseline. Repeating Rust reproduced the same baseline drift;
  the repeated Rust captures matched C exactly. This driver behavior is not a
  migration acceptance blocker. No pixel tolerance was added.
- `tests/gtk.c` covers real-window rendering/idle wakeup, natural size, fractional
  scale, unparent/reparent, 60 frame-clock ticks with no missed commits, node and
  drawer reuse, empty output and host/Scene lifetimes.
- ASan/UBSan checks passed with GL/Cairo (66 renderer/case combinations). LeakSanitizer
  initially reported Fontconfig/EGL allocations also reproduced by a standalone
  GTK window/GL renderer program without Graphite. The optional
  `tests/gtk-lsan.supp` suppresses only these library stacks; Graphite and generic
  GLib allocations remain checked. It is not enabled automatically.

For that sanitizer check:

```sh
cmake -S . -B /tmp/graphite-gtk-asan -DGRAPHITE_GTK=ON -DGRAPHITE_SANITIZERS=ON
LSAN_OPTIONS="suppressions=$PWD/tests/gtk-lsan.supp" GRAPHITE_GSK_RENDERERS=gl,cairo \
  cmake --build /tmp/graphite-gtk-asan --target check
```

The VM reports and logs are under `/tmp/graphite-gtk-parity/` and
`/tmp/graphite-gtk-*.log`. Measurements are compared at the Rust log's three-decimal
precision; image comparison checks all decoded RGBA channels exactly. These are
local VM results, not hosted CI or performance measurements. The C demo also has
a GTK frame-clock shell and PNG snapshots in `examples/demo/platform/gtk.c`.
Enabling GTK adds the `graphite_demo` executable and its two selftests to `check`;
see [demo usage and parity results](../../../examples/demo/README.md).
