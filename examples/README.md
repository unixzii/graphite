# Graphite examples

Each example is its own executable drawing one scene through the public C API.
They share one layer in `shared/`:

- `graphite_example` (`example.h`, `example.c`): the `example_desc` a scene
  fills in (name, window size, build/update/free callbacks), Scene lifecycle and
  construction helpers. It is platform-independent.
- `graphite_example_shell` (`cli.c`, `platform.h`, `platform/`): the command line,
  selftest, FPS report, snapshot writer and native window for the macOS, GTK,
  Windows or Web shell. An example's `main` only calls `example_main` with its
  description.

| Example | Size | Content |
|---|---|---|
| `showcase` | 960 × 640 | Six animated cards covering transforms, Canvas paths and gradients, shape layers, masks, clipping/scrolling, compositing and images. Its photo carousel pages in a loop; side photos shrink, tilt and blur (layer blur). |
| `ui` | 1000 × 600 | A control/gallery layout with animated switch, slider, caret, spinner and progress bar; its accent color turns gray while the window has no keyboard focus. Its labels, calendar and editor paragraph are real text: English labels with emoji, and a wrapped paragraph with styled spans, a line of Chinese and an ellipsis. Without a platform text system it draws placeholder bars instead. |
| `stress` | 800 × 800 | An 80 × 80 grid of independent square layers. Every three seconds, a ripple travels outward from the center, animating each cell's scale, radial displacement and color. |
| `glyphs` | 960 × 600 | Per-glyph text transitions in four looping labels: a clock, a price counter that rolls up or down, swapping phrases (CJK and emoji included) and a wrapped paragraph that reflows as words are inserted and removed. Every glyph is its own content layer drawn with `gr_canvas_draw_text_glyphs`; glyphs that stay glide to their new place while the others scale, slide, fade and blur (layer blur) in or out, driven by springs. Needs a platform text system. |
| `input` | 900 × 624 | Shows what the interactive host reports: a pointer pad with a crosshair, a dot colored by the pressed button and click ripples; cursor tiles that change the cursor on hover; a list that scrolls by wheel lines or touchpad points; a single-line text field on the host's text input (input methods, accent menus, key bindings, scrolling to the caret); and the last key's name, physical code and modifiers. Shortcuts with Alt or Super are left to the platform. |
| `mines` | 920 × 720 | A minesweeper: a size picker with a gliding focus ring, and a board that follows the window size (viewport callback), dents under a press, opens in ripples and sets mines off one after another. All motion is springs. |

The executables support macOS 14+, Linux with GTK 4.22+, Windows 11, and the
Web (built with Emscripten; see [Web](#web)). Where no shell is available, the
scene sources still compile but no executable is linked.

```sh
cmake -S . -B /tmp/graphite-c -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/graphite-c --target graphite_example_showcase graphite_example_ui graphite_example_stress graphite_example_glyphs graphite_example_mines
/tmp/graphite-c/examples/showcase/showcase
/tmp/graphite-c/examples/ui/ui --fps
/tmp/graphite-c/examples/stress/stress --fps
/tmp/graphite-c/examples/glyphs/glyphs
/tmp/graphite-c/examples/mines/mines
/tmp/graphite-c/examples/showcase/showcase --selftest
/tmp/graphite-c/examples/ui/ui --snapshot ui.png
```

Every example accepts the same options: `--selftest`, `--fps` or
`--snapshot <out.png>`.

`--snapshot` poses the scene at 2.4 seconds and renders at 1x.
Every shell attaches an interactive host and drives the scene from its
`GR_EVENT_FRAME` events (`shared/frames.c`), requesting the next frame from each
one: the host's display link on macOS, its frame clock's update phase on GTK,
its compositor clock on Windows and animation frames on the Web. Animations use frame time rather than an
assumed 60 Hz, so slow rendering or refresh-rate changes do not change animation
speed. Closing the window releases the host, and with it the frame handler,
before releasing state.

`--selftest` runs a real window for 60 frames and verifies that the
backend commits changes at its platform commit point without forcing a commit
each frame; successful output includes `uncommitted=[]` and the viewport the
host reported (a missing report fails). It also prints whether the host got
keyboard focus, which depends on the session and does not fail the test. GTK, Windows and the
Web check that previous-tick changes committed before the next tick. macOS commits when
the main run loop is about to sleep, like Core Animation's implicit transactions,
so it checks every such idle point instead (`idle_commits` must be nonzero). Ticks
delivered back to back without an idle point in between legitimately share one
commit; macOS reports them as `coalesced`. `--fps` reports the actual frame
rate every two seconds. Closing the window exits and releases scene resources.

Each example registers `graphite.examples.<name>` running its selftest, so CMake's
`check` target includes all of them on macOS, GTK, Windows and the Web. A graphical session
with the selected renderer available is required. Format checks include every
C/Objective-C file under `examples`.

## Adding an example

Create `examples/<name>/` with a source file that defines its state, build and
update callbacks, and `main`:

```c
static const example_desc desc = {"name", {800, 600}, build, update, free_state, NULL, NULL, NULL};
return example_main(argc, argv, &desc);
```

The last three fields are optional focus, input and viewport callbacks: the
shells pass keyboard focus changes, pointer, scroll and key events, and the
window's size to them (`ui` grays its accent color while the window is
unfocused; `input` visualizes input; `mines` resizes its root and lays out
again). The root keeps its build size unless the scene resizes it.
Scenes start focused, which is how snapshots render. While a window runs,
`ex->host` is its interactive host, for setting cursors.

Add `graphite_add_example(<name> <sources>...)` in its `CMakeLists.txt` and
`add_subdirectory(<name>)` in `examples/CMakeLists.txt`.

## Ownership

`example` holds references to layers made through the construction helpers until
`example_free`, which destroys the Scene before releasing those handles and then
the scene state. Icon drawers own copied glyph descriptions and label drawers own
their text layouts; `example` holds its text system until it is freed. The
animated Canvas drawer borrows the showcase state, which outlives the Scene.
Temporary paths/paints/strokes are released after recording or assignment; the
public Graphite API retains them.

## Platform setup

On Linux configure with `-DGRAPHITE_GTK=ON`. `GSK_RENDERER=gl` or `cairo` selects
snapshot/window rendering; software GL can avoid virtual GPU driver problems.
On Windows build from a Visual Studio developer prompt. Shared builds place the
executables and DLLs under `build/bin`. Compositor-clock updates require an awake
interactive display. See the [CMake guide](../docs/cmake.md).

Selftest callback rate is not a renderer throughput benchmark.

## Web

Configured with `emcmake`, each example builds `<name>.js` (one self-contained
script) and `<name>.html` next to it in the build directory. Open the page
straight from disk; it fills the window, and `?selftest` or `?fps` passes the
option:

```sh
emcmake cmake -S . -B ../graphite-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build ../graphite-wasm --parallel
open ../graphite-wasm/examples/showcase/showcase.html
```

The shell (`shared/platform/web.c` and `web.js`) attaches to `#graphite`,
creating one of the example's size on other pages, and hands the thread back
to the browser after starting frames. The selftest exits through
`emscripten_force_exit` with its status. `graphite.examples.<name>` runs it in
headless Chromium through `tests/web/run.mjs`. Snapshots are not available,
because the Web backend has no offscreen rendering. Text uses the Web text
system.
