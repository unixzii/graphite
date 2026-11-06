# C implementation

The C and Rust implementations coexist. CMake currently builds a static
`graphite::graphite` target containing immutable resources, Canvas recording,
Scene/Layer and the commit interface. On macOS, `graphite::macos` provides the
native Core Animation backend. Linux can enable the GTK 4.22 backend with
`-DGRAPHITE_GTK=ON`; Windows backend migration and text remain future work.

## Build and test

Use an out-of-tree build directory:

```sh
cmake -S . -B /tmp/graphite-c -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/graphite-c --target check
```

Address/undefined-behavior sanitizers can be enabled with
`-DGRAPHITE_SANITIZERS=ON` on Clang/GCC. Tests use always-active checks, including
in Release. The library requires C11 atomics; the platform toolchain must support
them. Headers are also compatible with C++ and Objective-C.

## Formatting and test harness

Use **clang-format 18** for all first-party C-family sources/headers in `src/`,
`include/`, `tests/`, `conformance/`, and `examples/demo/` (including Objective-C).
The root `.clang-format` enforces multiline function/control-flow bodies and
separation between definitions. Vendored files are excluded and stay unchanged.
If needed, configure `-DCLANG_FORMAT=/path/to/clang-format-18`.

```sh
cmake --build /tmp/graphite-c --target format       # Apply formatting
cmake --build /tmp/graphite-c --target format-check # Check without modifying
cmake --build /tmp/graphite-c --target check        # Format gate, build, verbose tests
```

Formatting is also registered with CTest; missing/wrong-version formatters fail
the check with an installation hint. Ordinary library compilation does not
require clang-format. The C GitHub Actions workflow runs `check` in Debug with
sanitizers and in Release; hosted results are separate from local validation.

`tests/test.h` and `tests/test.c` provide shared, always-active `CHECK`, `OK`,
and `NEAR`, plus a table-driven runner. Each case prints and flushes `[ RUN ]`
before execution and `[ OK ]` afterward. Failure reports the suite/case,
expression, file and line and aborts; numeric comparisons also show both values.
Worker threads must finish before a case returns. Register new cases with
`GR_TEST_CASE(function)` and suites with the CMake `graphite_add_suite` helper.

```sh
/tmp/graphite-c/tests/graphite_scene_tests --list
/tmp/graphite-c/tests/graphite_scene_tests teardown_reentry
```

There are currently 5 resource cases and 11 Scene/lifecycle cases. `check` uses
verbose CTest output so progress is visible even when all cases pass. An unknown
case name fails instead of silently running no tests. The lifecycle example also
uses the harness so its checks remain active in Release.

## Repository layout

- `include/graphite/`: public C headers.
- `src/core/`: C implementation and private headers.
- `src/platforms/macos/`: Objective-C Core Animation backend and hosting.
- `src/platforms/gtk/`: C GTK/GSK backend and hosting (optional SDK dependency).
- `conformance/`: backend-neutral cases, measurements and comparison tools.
- `tests/`: test harness, resource/Scene suites and lifecycle example.
- `vendor/`: pinned third-party sources and license information.
- `examples/demo/`: C scenes with macOS and GTK shells; `examples/demo-rs/` preserves the Rust demo.

Paths in this document are relative to the repository root unless stated otherwise.

## Implemented scope

- Allocation/overflow helpers, atomic shared-resource reference counts, ordinary
  builder/Canvas reference counts, unique resource IDs, private array storage,
  and a pinned private uthash dependency.
- Paint and Stroke with copied gradient stops/dashes and retained resources.
- Path/PathBuilder with rectangles, cubic ellipse approximation, selective
  rounded corners, validation, and reusable builders.
- Image with copied premultiplied RGBA8 bytes and dimensions/scale validation.
- Canvas and immutable DisplayList: balanced graphics state, path/image resource
  ownership, recording validation, and backend operation views.
- Scene/Layer with stable pointer handles, hierarchy/mask ownership, weak Scene
  links, invalidation on Scene teardown, and delayed native-identity retirement.
- Dirty-group propagation and coalescing, attachment/realization, two-pass
  reparenting batches, and full resolved property snapshots.
- Backend-driven drawer invocation during or after apply, stale-version rejection,
  read-only drawer access, and explicit userdata ownership/destruction.
- Transform resolution, image gravity, pixel snapping, custom shadow translation,
  and spread/rounded-corner shadow outlines.

`tests/lifecycle.c` now links and runs as part of `tests/scene.c`. Executable
resource tests live in `tests/resources.c`.

## Validation recorded

On macOS arm64 with AppleClang 21:

- Debug with ASan/UBSan and Release CTest passed.
- Coverage includes copied inputs, invalid-input atomicity, all rounded-corner
  combinations, recording retention after caller unref, unfinished recording
  cleanup, builder/Canvas reuse, and four-thread shared ref/unref/read access.
- uthash insertion/lookup/deletion and resource identity behavior passed.
- Every public header passed standalone C11, C++17 and Objective-C checks with
  strict warnings; a C++ consumer linked and ran against the C static library.
- macOS `leaks --atExit` reported zero leaked bytes for the resource tests.
- Scene tests pass in Debug ASan/UBSan and Release, including a recording backend
  that checks realization, two-pass child updates, masks, and release ordering.
  Cases cover detached changes/reattachment, value-equal setters, dirty dependencies,
  drawer versions, scale changes, callback cleanup reentry, deferred release during
  apply, external handles after teardown, and iterative release of a 4096-level tree.
- The Scene tests also report zero leaked bytes under macOS `leaks --atExit`.
  Clang's static analyzer reports no diagnostics for scene.c/layer.c/resolve.c.
- All 133 core public entry points have library definitions.
- The unchanged Rust core suite passed: 6 unit tests, 38 Scene tests and 1 doctest.
  The C recorder checks corresponding representative behaviors; this is not a
  claim that a native renderer or every Rust test has been mechanically ported.
- After formatting/harness integration, both Debug ASan/UBSan and Release
  `check` targets passed. Negative probes verified rejection of malformed
  formatting and an intentionally failing check compiled with `NDEBUG`, including
  failure diagnostics; listing/filtering/unknown-case behavior also passed.

Subsequent macOS validation passed Debug ASan/UBSan and Release `check`: format,
core suites, six native host/offscreen cases, four conformance helper cases,
Python comparison tests, and all 33 rendering cases. Both builds' 111 captures
exactly match the unchanged Rust backend's decoded RGBA pixels. All 39
measurement-log groups also match. The C demo adds showcase/UI real-window
selftests to `check` (nine CTest entries total on macOS); both demo snapshots
match the preserved Rust demo. See [demo usage](../examples/demo/README.md). See reproducible commands and scope in
[conformance validation](../conformance/README.md#macos-migration-validation).

Linux GTK validation has subsequently passed on the supplied Ubuntu VM: Debug
and Release, three GSK renderer variants with 333/333 captures matching Rust,
and GL/Cairo sanitizer checks with documented external-library leak exclusions.
See [GTK validation](platforms/gtk/README.md). Windows remains unvalidated. These
checks do not establish performance parity or pixel equality on other OS versions.

## Scene implementation notes

`core/model.h` defines the private pointer model. `core/scene.c` owns scheduling,
weak links, cleanup and batches; `core/layer.c` owns public property and hierarchy
operations; `core/resolve.c` translates raw properties to backend coordinates.

The registry and dirty queue are intrusive lists. Detached dirty bits are kept
without scheduling work until attachment. Batch storage scales with the queued
layers, not the entire registry; clean commits allocate nothing. Backend maps
borrow layer identities until their release notification completes, independently
of public ref counts. User cleanup runs only after a consistent state is published,
with Scene/allocation pins protecting the active operation.

The reviewed C contract intentionally expresses several Rust panic/no-op cases
as errors: structural violations and mutation during drawing return gr_result;
commit without a backend returns GR_ERROR_STATE without consuming changes;
recursive commit returns GR_ERROR_REENTRANT. It also validates finite numeric
inputs, rejects negative radii, and rejects stale deferred drawer versions.
Layer identity uses pointers, not Rust generation IDs. These are contract-level
differences, not accidental rendering or ownership changes.
