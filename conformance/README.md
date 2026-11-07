# Backend conformance

The suite contains **26 semantic cases, 7 measured cases and 8 text cases**,
each at **1×, 1.5× and 2×**.

## Organization

- `conformance.h`: backend-neutral attach/capture/destroy adapter and runner.
- `context.c`: per-case/per-scale Scene ownership, capture, checks and PNG output.
- `frame.c`: pixel probes, profiles, threshold crossings and integrated coverage.
- `cases.def`: shared registry; `cases.c` constructs the runtime case table.
- `semantic.c` / `measured.c`: rendering cases.
- `text.c`: text layout and rendering through the harness's native text
  system (metrics, line breaking, truncation, alignment, carets, threading and
  drawing). Fonts differ per platform, so these assert relations and
  invariants rather than exact metrics. A harness without `text_system`
  reports them as skipped.
- `macos.m`: Core Animation host adapter; executes on the main thread.
- `tests.c`: crossing/coverage and unique-name unit tests plus frame
  probe/profile and context-storage growth checks, using the shared test harness.
- `compare.py` / `compare_test.py`: same-backend pixel and metric comparison.

All captures are top-row-first premultiplied RGBA8. The PNG writer deliberately
stores these bytes unchanged; it uses uncompressed
DEFLATE blocks and introduces no image-codec dependency. Compare decoded pixels,
not compressed PNG files. Each suite run generates 114 PNGs when all cases run
(only one text case renders).

## Build and run

```sh
cmake -S . -B /tmp/graphite-c -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/graphite-c --target graphite_macos_conformance
GRAPHITE_CONFORMANCE_OUT=/tmp/graphite-c-captures \
  /tmp/graphite-c/conformance/graphite_macos_conformance
```

The default output base is `<repository>/target/conformance`. The harness name is
appended (`macos/`), followed by `<case>@<scale>x.png`; subsequent captures in the
same case/scale use `-1`, `-2`, etc. Use distinct reference and candidate output bases to
avoid overwriting the baseline.

Context Scene/layer/host/frame collections grow dynamically. Frames have stable
independent allocations so earlier returned frame pointers survive later renders.

The runner accepts positional substring filters matched against
`<harness>::<case>`, ignores flags, and continues after case
assertion failures to report the full set. It prints each case's start/result and
all measured quantities. Backend attach/capture failure fails the case; inability
to save a PNG emits a warning. Invalid test-scene
construction is a programming error and aborts with a source location.

```sh
/tmp/graphite-c/conformance/graphite_macos_conformance shadow
/tmp/graphite-c/conformance/graphite_conformance_tests
```

A GPU-capable macOS session is required for CARenderer/Metal capture. A sandboxed
or headless process without GPU access is not rendering evidence.

## Cross-run comparison

```sh
python3 -B conformance/compare.py /tmp/reference/macos /tmp/candidate/macos \
  --reference-log /tmp/reference.log --candidate-log /tmp/candidate.log \
  --report /tmp/comparison.json
```

This compares decoded pixels exactly and optionally compares measurement logs.
Use the same backend, OS and renderer configuration. Platform-specific numerical
acceptance is defined by the cases; cross-platform pixels may differ.

Linux uses `graphite_gtk_conformance` with `GRAPHITE_GSK_RENDERERS=gl,vulkan,cairo`.
Windows uses `graphite_windows_conformance` in an interactive desktop. See the
[GTK](../src/platforms/gtk/README.md) and
[Windows](../src/platforms/windows/README.md) guides for environment requirements.
