# Backend conformance

The C suite translates every case and measurement from
`crates/graphite-conformance`: **26 semantic cases and 7 measured cases**, each at
**1×, 1.5× and 2×**. Case names, geometry, colors, probes, thresholds, tolerances,
profile sampling, crossing interpolation, and coverage calculations follow the
Rust suite. Both implementations remain available for comparison.

## Organization

- `conformance.h`: backend-neutral attach/capture/destroy adapter and runner.
- `context.c`: per-case/per-scale Scene ownership, capture, checks and PNG output.
- `frame.c`: pixel probes, profiles, threshold crossings and integrated coverage.
- `cases.def`: shared registry; `cases.c` constructs the runtime case table.
- `semantic.c` / `measured.c`: all original cases.
- `macos.m`: Core Animation host adapter; executes on the main thread.
- `tests.c`: original crossing/coverage and unique-name unit tests plus frame
  probe/profile and context-storage growth checks, using the shared test harness.
- `compare.py` / `compare_test.py`: same-backend pixel and metric comparison.

All captures are top-row-first premultiplied RGBA8. The PNG writer deliberately
stores these bytes unchanged, as the Rust suite does; it uses uncompressed
DEFLATE blocks and introduces no image-codec dependency. Compare decoded pixels,
not compressed PNG files. Each suite run generates 111 PNGs when all cases run.

## Build and run

```sh
cmake -S . -B /tmp/graphite-c -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/graphite-c --target graphite_macos_conformance
GRAPHITE_CONFORMANCE_OUT=/tmp/graphite-c-captures \
  /tmp/graphite-c/conformance/graphite_macos_conformance
```

The default output base is `<repository>/target/conformance`. The harness name is
appended (`macos/`), followed by `<case>@<scale>x.png`; subsequent captures in the
same case/scale use `-1`, `-2`, etc. Use distinct output bases for C and Rust to
avoid overwriting the baseline.

Context Scene/layer/host/frame collections grow dynamically. Frames have stable
independent allocations so earlier returned frame pointers survive later renders.

The runner accepts positional substring filters matched against
`<harness>::<case>`, ignores flags like the Rust runner, and continues after case
assertion failures to report the full set. It prints each case's start/result and
all measured quantities. Backend attach/capture failure fails the case; inability
to save a PNG emits a warning, matching the Rust output policy. Invalid test-scene
construction is a programming error and aborts with a source location.

```sh
/tmp/graphite-c/conformance/graphite_macos_conformance shadow
/tmp/graphite-c/conformance/graphite_conformance_tests
```

A GPU-capable macOS session is required for CARenderer/Metal capture. A sandboxed
or headless process without GPU access is not rendering-parity evidence.

## macOS migration validation

Validated on 2026-11-07, macOS 27.2 arm64, AppleClang 21, with a real GUI/Metal
session. Rust source was unchanged from the migration branch baseline
`67b1107`. The C implementation and tests coexist with that reference.

| Run | Cases | Captures exactly equal to Rust | Measurement-log differences |
|---|---:|---:|---:|
| Rust repeat (baseline stability) | 33 passed | 111 / 111 | 0 / 39 groups |
| C Debug ASan/UBSan | 33 passed | 111 / 111 | 0 / 39 groups |
| C Release | 33 passed | 111 / 111 | 0 / 39 groups |

Equality is strict on all decoded RGBA channels and dimensions, including
antialiased edges, with no allowed per-pixel tolerance. The case-level semantic
and measured tolerances are independently kept exactly as in Rust. Measurement
logs have the original three-decimal precision; they are not a higher-precision
numeric dump. PNG compression and metadata are not compared.

The full C `check` target also passed its seven CTest entries: formatting, resource
and Scene suites, six native macOS tests (including a real NSWindow), four helper
cases, Python comparator tests, and the complete rendering suite. During the port,
the offscreen container's anchor/frame setup was corrected to match Rust, and
backend shutdown was made explicit so autorelease timing cannot outlive layer
identity leases. No tolerance or reference image was changed to obtain parity.

To reproduce, use fresh, separate output directories:

```sh
GRAPHITE_CONFORMANCE_OUT=/tmp/graphite-parity/rust \
  cargo test -p graphite-macos --test conformance -- --nocapture --test-threads=1 \
  > /tmp/graphite-rust.log 2>&1
cmake -S . -B /tmp/graphite-c -DCMAKE_BUILD_TYPE=Debug -DGRAPHITE_SANITIZERS=ON
GRAPHITE_CONFORMANCE_OUT=/tmp/graphite-parity/c \
  cmake --build /tmp/graphite-c --target check > /tmp/graphite-c.log 2>&1
python3 -B conformance/compare.py /tmp/graphite-parity/rust /tmp/graphite-parity/c \
  --reference-log /tmp/graphite-rust.log --candidate-log /tmp/graphite-c.log \
  --report /tmp/graphite-parity/report.json
```

The comparator rejects missing/extra captures, any pixel difference, or differing
measurement groups with a nonzero exit status. It needs no third-party Python
packages. Results describe this suite on this OS/GPU environment; they are not a
claim about untested rendering features, other platforms, or performance.

## GTK migration validation

Enable `GRAPHITE_GTK=ON` for `graphite_gtk_conformance`; `GRAPHITE_GSK_RENDERERS`
selects `gl,vulkan,cairo` (default `gl,vulkan`). On the supplied Ubuntu 26.04.1
arm64/GTK 4.22.4 VM, Debug and Release pass all 99 renderer/case combinations.
With software GL and Vulkan, all 333 captures and 117 measurement-log groups
match the Rust GTK reference. The comparator keys metrics by renderer as well
as case, scale and label, preventing cross-renderer results from being mixed.

The default VM Vulkan driver's transient output was reproduced with Rust itself
and is not used as a blocker. GL/Cairo sanitizer checks pass with narrow, explicit
Fontconfig/EGL leak suppressions confirmed using a standalone GTK program. See
[GTK validation and reproduction commands](../src/platforms/gtk/README.md).

## Windows migration validation

Windows builds provide `graphite_windows_conformance`. Use an interactive, awake
desktop session; an SSH session alone cannot validate DWM output. On the supplied
ARM64 Windows VM, MSVC Debug and Release each pass all 33 cases at 1x, 1.5x and
2x. All 111 captures and 39 measurement-log groups exactly match the unchanged
Rust Windows backend. Both demo snapshots also match in both builds. See the
[Windows build and validation guide](../src/platforms/windows/README.md).
