# Graphite examples

Each example is its own executable drawing one scene through the public C API.
They share one layer in `shared/`:

- `graphite_example` (`example.h`, `example.c`): the `example_desc` a scene
  fills in (name, window size, build/update/free callbacks), Scene lifecycle and
  construction helpers. It is platform-independent.
- `graphite_example_shell` (`cli.c`, `platform.h`, `platform/`): the command line,
  selftest, FPS report, snapshot writer and native window for the macOS, GTK or
  Windows shell. An example's `main` only calls `example_main` with its
  description.

| Example | Size | Content |
|---|---|---|
| `showcase` | 960 × 640 | Six animated cards covering transforms, Canvas paths and gradients, shape layers, masks, clipping/scrolling, compositing and images. Its photo carousel pages in a loop; side photos shrink, tilt and blur (layer blur). |
| `ui` | 1000 × 600 | A control/gallery layout with animated switch, slider, caret, spinner and progress bar; its accent color turns gray while the window has no keyboard focus. Its labels, calendar and editor paragraph are real text: English labels with emoji, and a wrapped paragraph with styled spans, a line of Chinese and an ellipsis. Without a platform text system it draws placeholder bars instead. |
| `stress` | 800 × 800 | An 80 × 80 grid of independent square layers. Every three seconds, a ripple travels outward from the center, animating each cell's scale, radial displacement and color. |
| `glyphs` | 960 × 600 | Per-glyph text transitions in four looping labels: a clock, a price counter that rolls up or down, swapping phrases (CJK and emoji included) and a wrapped paragraph that reflows as words are inserted and removed. Every glyph is its own content layer drawn with `gr_canvas_draw_text_glyphs`; glyphs that stay glide to their new place while the others scale, slide, fade and blur (layer blur) in or out, driven by springs. Needs a platform text system. |

The executables support macOS 14+, Linux with GTK 4.22+, and Windows 11. Where
no native shell is available, the scene sources still compile but no executable
is linked.

```sh
cmake -S . -B /tmp/graphite-c -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/graphite-c --target graphite_example_showcase graphite_example_ui graphite_example_stress graphite_example_glyphs
/tmp/graphite-c/examples/showcase/showcase
/tmp/graphite-c/examples/ui/ui --fps
/tmp/graphite-c/examples/stress/stress --fps
/tmp/graphite-c/examples/glyphs/glyphs
/tmp/graphite-c/examples/showcase/showcase --selftest
/tmp/graphite-c/examples/ui/ui --snapshot ui.png
```

Every example accepts the same options: `--selftest`, `--fps` or
`--snapshot <out.png>`.

`--snapshot` poses the scene at 2.4 seconds and renders at 1x.
Every shell attaches an interactive host and drives the scene from its
`GR_EVENT_FRAME` events (`shared/frames.c`), requesting the next frame from each
one: the host's display link on macOS, its frame clock's update phase on GTK and
its compositor clock on Windows. Animations use frame time rather than an
assumed 60 Hz, so slow rendering or refresh-rate changes do not change animation
speed. Closing the window releases the host, and with it the frame handler,
before releasing state.

`--selftest` runs a real window for 60 frames and verifies that the
backend commits changes at its platform commit point without forcing a commit
each frame; successful output includes `uncommitted=[]` and the viewport the
host reported (a missing report fails). It also prints whether the host got
keyboard focus, which depends on the session and does not fail the test. GTK and Windows check
that previous-tick changes committed before the next tick. macOS commits when
the main run loop is about to sleep, like Core Animation's implicit transactions,
so it checks every such idle point instead (`idle_commits` must be nonzero). Ticks
delivered back to back without an idle point in between legitimately share one
commit; macOS reports them as `coalesced`. `--fps` reports the actual frame
rate every two seconds. Closing the window exits and releases scene resources.

Each example registers `graphite.examples.<name>` running its selftest, so CMake's
`check` target includes all of them on macOS, GTK and Windows. A graphical session
with the selected renderer available is required. Format checks include every
C/Objective-C file under `examples`.

## Adding an example

Create `examples/<name>/` with a source file that defines its state, build and
update callbacks, and `main`:

```c
static const example_desc desc = {"name", {800, 600}, build, update, free_state, NULL};
return example_main(argc, argv, &desc);
```

The last field is an optional focus callback: the shells pass keyboard focus
changes to it (`ui` grays its accent color while the window is unfocused).
Scenes start focused, which is how snapshots render.

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
