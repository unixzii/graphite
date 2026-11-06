# Graphite demo

The C demo contains the same two scenes and animations as the Rust reference in
`../demo-rs`. Scene code uses only the public Graphite C API; the native event loop
and PNG snapshot writer live in `platform/macos.m`.

- `showcase` (960 × 640): six animated cards covering transforms, Canvas paths and
  gradients, shape layers, masks, clipping/scrolling, compositing and images.
- `ui` (1000 × 600): the original control/gallery layout with placeholder text
  bars, animated switch, slider, caret, spinner and progress bar.

The windowed C executable requires macOS 14 or later and uses the available C backend.
The scene library builds independently of the native shell. The old Rust demo
retains its GTK/Windows/macOS shells, with its Cargo example now named `demo-rs`.

```sh
cmake -S . -B /tmp/graphite-c -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/graphite-c --target graphite_demo
/tmp/graphite-c/examples/demo/demo
/tmp/graphite-c/examples/demo/demo --scene ui
/tmp/graphite-c/examples/demo/demo --scene ui --fps
/tmp/graphite-c/examples/demo/demo --selftest
/tmp/graphite-c/examples/demo/demo --scene ui --selftest
/tmp/graphite-c/examples/demo/demo --snapshot showcase.png
/tmp/graphite-c/examples/demo/demo --scene ui --snapshot ui.png
```

`--snapshot` poses the scene at 2.4 seconds and renders at 1x, matching Rust.
The window creates a `CADisplayLink` associated with its display and registers it
on the main run loop in common modes. Animation time comes from display-link
timestamps rather than an assumed 60 Hz, so refresh-rate changes do not change
animation speed. Closing the window invalidates the link before releasing state.

`--selftest` runs a real window for 60 display callbacks and verifies that
previous-tick changes committed before the next tick; successful output includes
`uncommitted=[]`. It does not force a commit each tick. `--fps` reports actual display-callback
rate every two seconds. Closing the window exits and releases scene resources.

CMake's `check` target includes both native selftests on macOS. A GUI/Metal-capable
session is required. Format checks include all C/Objective-C files in this demo.

## Ownership

The demo holds references to constructed layers while animation state needs them.
Scene destruction occurs before releasing these handles or callback state.
Icon drawers own copied glyph descriptions; the animated Canvas drawer borrows
its enclosing demo state, which outlives the Scene. Temporary paths/paints/strokes
are released after recording or assignment; the public Graphite API retains them.

## Rust comparison

Generate each pair in fresh output directories and compare decoded RGBA pixels:

```sh
mkdir -p /tmp/demo-parity/rust /tmp/demo-parity/c
cargo run --example demo-rs -- --snapshot /tmp/demo-parity/rust/showcase.png
cargo run --example demo-rs -- --scene ui --snapshot /tmp/demo-parity/rust/ui.png
/tmp/graphite-c/examples/demo/demo --snapshot /tmp/demo-parity/c/showcase.png
/tmp/graphite-c/examples/demo/demo --scene ui --snapshot /tmp/demo-parity/c/ui.png
python3 -B conformance/compare.py /tmp/demo-parity/rust /tmp/demo-parity/c
```

Both snapshots were verified pixel-identical to Rust on macOS 27.2 arm64 with
AppleClang 21. PNG metadata/compression can differ; the comparison uses decoded
pixels without tolerances. This snapshot comparison covers the 2.4-second pose;
the window selftests separately check animation scheduling.

Validation also passed both 60-tick real-window selftests in Release and Debug
ASan/UBSan (`uncommitted=[]`, 2x backing scale), the full nine-entry CTest check,
four invalid-argument cases, and a live `--fps` report. macOS `leaks --atExit`
reported zero leaked bytes for the UI selftest. All 16 Rust source files were
verified unchanged byte-for-byte after moving to `demo-rs`.
