# Graphite demo

Scene code uses the public C API; native event loops and PNG writers live in
platform-specific macOS, GTK and Windows shells.

- `showcase` (960 × 640): six animated cards covering transforms, Canvas paths and
  gradients, shape layers, masks, clipping/scrolling, compositing and images.
- `ui` (1000 × 600): a control/gallery layout with placeholder text
  bars, animated switch, slider, caret, spinner and progress bar.

The windowed C executable supports macOS 14+, Linux with GTK 4.22+, and Windows 11.
The scene library builds independently of the native shell.

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

`--snapshot` poses the scene at 2.4 seconds and renders at 1x.
On macOS, the window registers a display-associated `CADisplayLink` on the main
run loop in common modes. On GTK, a widget frame-clock tick updates the Scene
before the backend commits in layout. The Windows shell posts compositor-clock events from a worker to the UI thread.
All shells use elapsed time
rather than an assumed 60 Hz, so slow rendering or refresh-rate changes do not
change animation speed. Closing the window removes its update callback before
releasing state.

`--selftest` runs a real window for 60 display callbacks and verifies that
previous-tick changes committed before the next tick; successful output includes
`uncommitted=[]`. It does not force a commit each tick. `--fps` reports actual display-callback
rate every two seconds. Closing the window exits and releases scene resources.

CMake's `check` target includes both native selftests on macOS, GTK and Windows. A graphical
session with the selected renderer available is required. Format checks include all C/Objective-C files in this demo.

## Ownership

The demo holds references to constructed layers while animation state needs them.
Scene destruction occurs before releasing these handles or callback state.
Icon drawers own copied glyph descriptions; the animated Canvas drawer borrows
its enclosing demo state, which outlives the Scene. Temporary paths/paints/strokes
are released after recording or assignment; the public Graphite API retains them.

## Platform setup

On Linux configure with `-DGRAPHITE_GTK=ON`. `GSK_RENDERER=gl` or `cairo` selects
snapshot/window rendering; software GL can avoid virtual GPU driver problems.
On Windows build from a Visual Studio developer prompt. Shared builds place the
executable and DLLs under `build/bin`. Compositor-clock updates require an awake
interactive display. See the [CMake guide](../../docs/cmake.md).

The macOS showcase selftest has intermittently reported an uncommitted second
frame during repeated runs. This scheduling issue remains under investigation;
see [roadmap](../../docs/roadmap.md). Selftest callback rate is not a renderer
throughput benchmark.
