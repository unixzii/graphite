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
  above children. Rounded clipping and opaque-border midline insets match Rust.
- Native shape geometry and trimming use separate fill/stroke sprite shapes.
  Linear/radial brushes preserve absolute shape and relative layer coordinates.
- Alpha masks use a VisualSurface and a custom `IGraphicsEffectD2D1Interop`
  description of Direct2D's alpha-mask effect, avoiding a Win2D dependency.
- Images share drawing surfaces by resource ID. Drawers are recorded only when
  version/bounds/scale changes and released after Direct2D replay. Stroke replay
  widens paths and fills the resulting outline, preserving the reference's
  anti-aliasing workaround for the VM GPU.
- Shadow masks use Direct2D drawing surfaces, with the reference's blur-radius
  factor of 3 times sigma and retained outline keys for scale-only changes.
- DispatcherQueue coalesces pending commits via weak Scene links. Backend maps
  borrow layer identities only within core's release-notification leases.
- HWND hosts are input-transparent child windows; their DPI tracks the parent.
  Visual hosts use a ContainerVisual from the thread's compositor. Final host
  unref detaches its visual/destroys its window, as in Rust.
- A per-thread context shares compositor/devices and remains alive for process
  teardown. dcomp.dll is pinned to avoid releases jumping into an unloaded DLL,
  matching the original backend's shutdown workaround. This is an intentional
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
C:\graphite-build\examples\demo\demo.exe --scene ui
```

Use `Release` in a separate build directory for optimized validation. Windows
builds enable this backend and the native demo automatically. The CMake sanitizer
option is not enabled for this MSVC ARM64 configuration; Debug runtime checks and
Release execution are reported separately from macOS/Linux sanitizer evidence.

## Demo clock

`examples/demo/platform/windows.cpp` waits for the Windows 11 compositor clock
on a worker and posts at most one outstanding frame message to the window thread.
Only that UI thread changes the Scene. Animation uses elapsed steady-clock time,
not a fixed frame-count divisor. Closing the window signals/joins the clock worker
before callback state is released. WIC writes the 1x snapshot's exact RGBA content.

The compositor clock pauses when the display is off (the OS returns
`STATUS_GRAPHICS_PRESENT_OCCLUDED`). Automated selftests require an **awake**
interactive display; they intentionally do not substitute a timer to hide that
condition. See Microsoft's [compositor clock documentation](https://learn.microsoft.com/en-us/windows/win32/directcomp/compositor-clock/compositor-clock).

## Validation

Validated on the supplied `ssh ktiays_win` ARM64 VM with Visual Studio 18 / MSVC
19.51.36260 and SDK 10.0.26100.0. Sources are in
`C:\Users\ktiays\graphite-c-migration`; Debug and Release builds are in
`C:\Users\ktiays\graphite-c-build` and `C:\Users\ktiays\graphite-c-release`.

SSH runs in session 0, which is not a rendering test environment. Tests were run
through the VM's existing `bin\run-gui.ps1` in console session 1. A validation-only
wrapper temporarily kept the display awake and restored its execution state.
No persistent display power policy was changed.

- Both Debug and Release passed all eight registered CTest entries: formatting,
  resources, Scene, conformance helpers, native Windows tests, all 33 rendering
  cases at three scales, and both demo selftests.
- Both builds' **111/111 conformance captures** match the unchanged Rust backend
  exactly, with **39 identical measurement-log groups** (three-decimal precision).
- Showcase/UI snapshots match Rust pixel-for-pixel in Debug and Release.
- Native tests cover HWND placement, DPI, automatic/idle commits, movement,
  resizing, visual attachment/detachment, alpha readback, invalid arguments,
  and host/Scene lifetimes. Demo selftests report 60 frames with no missed commits
  at the VM's 1.75 backing scale.
- The shared test harness now supports C++ and avoids the Windows SDK's `NEAR`
  macro. Recorder arrays moved to the heap rather than raising Windows' default
  thread stack size. The unchanged C core tests pass with that fixture.

Reports/captures are under `C:\Users\ktiays\graphite-validation`. Raw logs are
captured through cmd redirection so PowerShell's text encoding does not alter
Unicode measurement labels. The comparison tool runs on the development host.
These are local VM results, not hosted CI, Windows ASan, or performance claims.
