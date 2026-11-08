# Windows.UI.Composition backend

`graphite::windows` (`graphite_windows`) implements the C backend interface using
C++20, the Windows SDK's C++/WinRT projections, Direct2D and Direct3D 11. No Win2D
or third-party Windows binding library is required. Public C hosting/readback
functions are in `include/graphite/windows.h`.

The backend targets Windows 11. Build from a Visual Studio developer environment
with a matching Windows SDK and compiler architecture. MSVC's C11 atomic support
is enabled for the C core with `/experimental:c11atomics`; `/utf-8` keeps source
and diagnostic text consistent across platforms. C++ compilation uses `/W4 /WX`.

## Rendering and ownership

- Each layer has outer/body/clipped/sublayer composition containers. Group opacity
  and transforms apply at the outer; shadow escapes the body clip; borders remain
  above children. Rounded clipping uses opaque-border midline insets.
- Native shape geometry and trimming use separate fill/stroke sprite shapes.
  Linear/radial brushes preserve absolute shape and relative layer coordinates.
- Masks run the body through a LayerVisual whose effect is Direct2D's alpha-mask
  effect (a custom `IGraphicsEffectD2D1Interop` description, avoiding a Win2D
  dependency) over a VisualSurface of the mask: a filled ShapeVisual for a path
  mask, a gradient SpriteVisual over the box for a gradient mask.
- Layer blur wraps the outer's contents in a LayerVisual whose effect is
  Direct2D's Gaussian blur (σ times the backing scale, in device pixels).
- LayerVisuals clip to their size, so after each batch the backend resizes
  every mask and blur LayerVisual to cover what its subtree draws (contents,
  sublayers and shadows, within the mask's bounds), and for blur a 3σ margin divided by the smallest scale of
  the transforms above it.
- Images share drawing surfaces by resource ID. Drawers are recorded only when
  version/bounds/scale changes and released after Direct2D replay. Stroke replay
  widens paths and fills the resulting outline, avoiding aliased native strokes on affected VM GPUs.
- Shadow masks use Direct2D drawing surfaces, with a blur-radius
  factor of 3 times sigma and retained outline keys for scale-only changes.
- `text.cpp` implements the DirectWrite text system
  (`gr_windows_text_system_new`); `draw.cpp` replays its layouts with
  `DrawTextLayout`. See the [backend notes](../../../docs/backends/windows.md#text).
- DispatcherQueue coalesces pending commits via weak Scene links. Backend maps
  borrow layer identities only within core's release-notification leases.
- HWND hosts are input-transparent child windows; their DPI tracks the parent.
  Visual hosts use a ContainerVisual from the thread's compositor. Final host
  unref detaches its visual/destroys its window, as documented by the hosting API.
- HWND hosts are interactive. The first frame request starts a worker thread that
  waits on `DCompositionWaitForCompositorClock` while a frame is requested and
  posts each tick to the host window; the UI thread delivers `GR_EVENT_FRAME`
  (QPC time, compositor frame period) and then commits at once, so the frame's
  changes reach the next compositor frame instead of waiting for the
  DispatcherQueue. `WM_SIZE`/DPI changes report `GR_EVENT_VIEWPORT` and
  `WM_SETFOCUS`/`WM_KILLFOCUS` report focus. Interactive host windows answer
  `WM_NCHITTEST` with the client area (others stay `HTTRANSPARENT`) and report
  mouse messages (pen-promoted ones as pens) with `SetCapture` for drags and
  `TrackMouseEvent` for leaving, wheel messages in lines (fractional for
  precision touchpads; unconsumed ones go on to the parent) and key messages:
  `keys.cpp` maps set-1 scan codes (with the E0 prefix) and virtual keys, and
  `MapVirtualKey` gives the unmodified text. Click counts follow the system
  double-click time and rectangle. Presses take focus. Cursors are set from
  `WM_SETCURSOR`. Each input message holds a host reference, so a handler that
  releases the host cannot destroy the window mid-message. Text input requests
  are accepted but not applied yet.
- A per-thread context shares compositor/devices and remains alive for process
  teardown. dcomp.dll is pinned to avoid releases jumping into an unloaded DLL,
  to protect shutdown releases. This is an intentional
  context lifetime policy, not a claim of whole-process zero allocations.

Offscreen capture uses a visible, unactivated popup outside the desktop. It waits
for composition commits and reads DWM content with `PrintWindow`. Two captures,
over black and white, reconstruct premultiplied alpha. A screenshot therefore
requires an interactive desktop, even though no test window is on screen.

## Build and run

From an interactive Visual Studio developer prompt:

```bat
cmake -S . -B C:\graphite-build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCLANG_FORMAT=C:/path/to/clang-format-18.exe
cmake --build C:\graphite-build --target check
C:\graphite-build\conformance\graphite_windows_conformance.exe
C:\graphite-build\examples\ui\ui.exe
```

Use `Release` in a separate build directory for optimized validation. Windows
builds enable this backend and the native examples automatically. The CMake sanitizer
option is not enabled for this MSVC ARM64 configuration; Debug runtime checks and
Release execution are reported separately from macOS/Linux sanitizer evidence.

## Example clock

`examples/shared/platform/windows.cpp` waits for the Windows 11 compositor clock
on a worker and posts at most one outstanding frame message to the window thread.
Only that UI thread changes the Scene. Animation uses elapsed steady-clock time,
not a fixed frame-count divisor. Closing the window signals/joins the clock worker
before callback state is released. WIC writes the 1x snapshot's exact RGBA content.

The compositor clock pauses when the display is off (the OS returns
`STATUS_GRAPHICS_PRESENT_OCCLUDED`). Automated selftests require an **awake**
interactive display; they intentionally do not substitute a timer to hide that
condition. See Microsoft's [compositor clock documentation](https://learn.microsoft.com/en-us/windows/win32/directcomp/compositor-clock/compositor-clock).

## Tests and limitations

Native tests cover HWND placement, DPI, automatic/idle commits, movement,
resizing, visual attachment/detachment, alpha readback and host/Scene lifetimes.
Conformance covers 36 cases at three scales. Run these in an interactive desktop;
SSH session 0 cannot provide rendering evidence. A temporarily awake display is
required for compositor-clock example selftests.

Device-loss recovery is not implemented, and shadow/drawing raster surfaces
are per-layer. Effect LayerVisuals are re-measured on every batch, walking the
subtrees of masked and blurred layers. Validate edge quality and monitor
DPI transitions on the target hardware. The per-thread context lifetime policy
is intentional; whole-process leak counts require accounting for it.
