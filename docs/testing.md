# Testing

Configure and run the enabled suites with:

```sh
cmake -S . -B /tmp/graphite-test -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/graphite-test --target check
```

`check` builds tests and enabled example/conformance targets, runs clang-format 18
validation and invokes CTest with progress output. `BUILD_TESTING=OFF` disables
tests; examples and conformance have independent build switches. See
[CMake packages](cmake.md).

## Core

`tests/resources.c` checks immutable copies, recording ownership, validation and
cross-thread resource references. `tests/scene.c` checks a recording backend and
mirror tree, commit ordering, reentry, masks, deferred drawers and teardown.
`tests/text.c` checks attributed text validation, span copies and UTF-16
mapping, and drives text layouts through a fake text system to check caret
normalization, lifetimes and text recording.
`tests/events.c` drives the host event API through a recording host: handler
ownership and replacement, frame request coalescing, reentrant dispatch queues,
release during delivery and event validation.
`tests/lifecycle.c` demonstrates ownership through an entire Scene lifetime.
The shared harness supports listing/filtering and reports each case; failures
remain active in Release builds. See [coverage notes](core-test-audit.md).

```sh
ctest --test-dir /tmp/graphite-test -R 'graphite.(resources|scene|text|events)$' --output-on-failure
cmake -S . -B /tmp/graphite-asan -DGRAPHITE_SANITIZERS=ON
cmake --build /tmp/graphite-asan --target graphite_resource_tests graphite_scene_tests graphite_text_tests \
    graphite_event_tests
ctest --test-dir /tmp/graphite-asan -R 'graphite.(resources|scene|text|events)$' --output-on-failure
```

Sanitizers require Clang/GCC. Windows MSVC builds use separate Debug/Release
checks. GTK's optional external-library leak suppressions are documented in
[its backend guide](../src/platforms/gtk/README.md).

## Native and rendering

Platform tests verify hosting, scaling, idle scheduling, snapshots and lifetime.
They need a GUI/renderer session: macOS needs Metal/WindowServer, GTK a graphical
display, and Windows an interactive desktop rather than SSH session 0. The Web
backend's test runs in headless Chromium through Playwright, and an Emscripten
build runs the core suites under node; see the
[Web backend guide](../src/platforms/web/README.md).

[Conformance](../conformance/README.md) contains semantic assertions and measured
edge/blur/coverage probes at three scales. Compare captures from the same backend
and renderer with `conformance/compare.py`; cross-platform output is not required
to be pixel-identical. Keep visual review separate from numerical acceptance.

Demo selftests run 60 callbacks and detect changes left uncommitted at the
backend's commit point: before the next tick on GTK and Windows, and at every
main run-loop idle point on macOS. They test scheduling, not throughput.

## Package consumers and benchmarks

`tests/package` independently configures C or C++ projects using installed targets.
Run against both static/shared packages and relocated installation prefixes.
[Benchmarks](../benchmarks/README.md) measure core work and native example processes;
keep timing runs separate from instrumented allocation runs and builds.

## Text tests

`tests/text.c` drives the core text API through a fake text system. Native text
systems are tested by the conformance suite's `text` cases, which every
backend with a text system runs: wrapping, line limits, ellipsis, bidi,
grapheme boundaries, caret and selection queries, concurrency, drawing, and
glyph lists and glyph-range drawing (which must redraw the layout).
They assert relations across platforms, including the behaviors where native
engines disagree (ellipsis at paragraph breaks, bidi caret affinity, letter
spacing); captures cover rasterization per platform.
