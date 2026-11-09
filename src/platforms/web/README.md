# Web (DOM) backend

`graphite::web` (`graphite_web`) hosts a Scene in a web page. It is built with
Emscripten for wasm32 and selected automatically when configuring with
`emcmake`. All calls run on the browser's main thread. The library is static
only. See the [design](../../../docs/backends/web.md) for the plan and the
measurements behind it.

**Status: renders layers, drawings and text, and delivers events.** Layer
geometry, opacity, visibility, background, corner radii, borders,
`masks_to_bounds`, masks, shadows, blur, and shape, gradient, image and drawn
contents all render, and `gr_web_text_system_new` lays out and draws text. Hosts are
interactive: frames, timers, viewport (size and `devicePixelRatio`), focus, mouse and
pen pointers, wheel and keys, cursors, and text input (typing, input methods,
paste, editing keys). Touch is not implemented yet. Every example runs in the
browser.

## Implementation

- `host.c`: `gr_web_attach_to_element` (declared in
  [`web.h`](../../../include/graphite/web.h)). It creates the host element inside
  the element a target string names (see [Choosing the host
  element](#choosing-the-host-element)), installs the backend, sets the backing
  scale from `devicePixelRatio` and returns an interactive host. The runtime
  reports DOM events through the `gr_web_on_*` exports, which build `gr_event`s;
  a requested frame goes out from the backend's tick (`gr_web_tick`), just
  before that animation frame's commit.
- `backend.c`: the backend vtable. `request_commit` asks the runtime for one
  animation frame, which calls back into `gr_web_commit`. `apply` encodes the
  commit batch as a stream of doubles (commands and operands are documented in
  `internal.h`) and passes it to the runtime in a single call. Layer identities
  are their pointers, valid until their `RELEASE` command. Drawn layers are
  drawn through the drawer provider during `apply`, only when their display
  version or drawing bounds changed; their display lists are sent as ops.
- `text.c` and `text.js`: the text system (`gr_web_text_system_new`). `text.c`
  converts between the core's types and number arrays; `text.js` measures
  layouts in a hidden element, answers every query and draws layouts and glyph
  ranges for the runtime's drawings. The [design](../../../docs/backends/web.md#text)
  describes the algorithm. Installed as `share/graphite/graphite_web_text.js`.
- `runtime.js`: the DOM side, an Emscripten JS library. Every executable that
  links `graphite_web` gets it through the target's `--js-library` link option.
  It keeps one node per realized layer (the element tree is described in
  `GRWeb.node` and the [design](../../../docs/backends/web.md#layers)) and
  applies the commands in commit-contract order, including the two children
  passes. Each host has one animation-frame tick, requested by frame requests
  and commit requests alike, and its event listeners: pointer events with
  capture during drags, `wheel`, `keydown`/`keyup` (W3C `code` values map
  straight onto `gr_key_code`), `focusin`/`focusout`, a `ResizeObserver`, and
  `devicePixelRatio` changes caught by a media query, `resize` and each tick.
  Text input keeps the shared state in the runtime (`host.text`) and a hidden
  field that the browser types into; see the
  [design](../../../docs/backends/web.md#text-input).
  Calls into C go through `GRWeb.call`, which ends Emscripten's exit and unwind
  exceptions without `callUserCallback`'s implicit exit: events also fire while
  C is calling in (`focus()` from `request_focus`, the first viewport report).
  Installed as `share/graphite/graphite_web.js`.

## Build and test

Install Emscripten (for example `brew install emscripten`, or the emsdk), then:

```sh
(cd tests/web && npm ci && npx playwright install chromium)
emcmake cmake -S . -B ../graphite-wasm -DCMAKE_BUILD_TYPE=Debug
cmake --build ../graphite-wasm --parallel
ctest --test-dir ../graphite-wasm --output-on-failure
```

- Core, text, event and conformance-helper tests run under node through
  CMake's cross-compiling emulator. Without `-pthread`, `shared_resources`
  runs its workers one after another. Add `-DCMAKE_C_FLAGS=-pthread` with
  `-DCMAKE_EXE_LINKER_FLAGS="-pthread -sPROXY_TO_PTHREAD"` to run them on
  threads.
- `graphite.web` (`tests/web.c`) runs in headless Chromium:
  `tests/web/run.mjs` loads the single-file test program into a page with a
  `#host` element and exits with its status. The test is registered only
  after `npm ci` has run in `tests/web`. Its DOM queries live in `tests/web.js`,
  because clang-format rewrites JavaScript written inline in C.
- `graphite.conformance.web` (`conformance/web.c` and `web.js`) runs the shared
  conformance suite in headless Chromium through the same runner; captures go
  to `target/conformance/web`. The text thread-safety case is skipped as
  unsupported. Pass case-name filters after the program:
  `node tests/web/run.mjs ../graphite-wasm/conformance/graphite_web_conformance.js shadow`.
- `graphite.web` also drives the interactive host with synthetic DOM events
  (pointer, wheel, key, focus), checks that each animation frame delivers its
  frame event before committing the changes made in it, and that a
  `devicePixelRatio` change reaches the Scene. It also draws text from a layout
  that its drawer releases right after drawing.
- Every example builds as `<name>.js` with a `<name>.html` page, and
  `graphite.examples.<name>` runs its selftest in headless Chromium; see the
  [examples guide](../../../examples/README.md#web).

## Using an installed package

The Emscripten toolchain only searches its own sysroot for packages, so point
CMake at the installed config directly:

```sh
emcmake cmake -S my_app -B build -Dgraphite_DIR=/path/to/prefix/lib/cmake/graphite
```

```cmake
find_package(graphite 0.0.1 CONFIG REQUIRED COMPONENTS web)
target_link_libraries(my_app PRIVATE graphite::web)
```

## Choosing the host element

`gr_web_attach_to_element` takes a string, because C cannot hold a JavaScript
object. The string resolves the way Emscripten's html5 event targets do:

- A name registered in `specialHTMLTargets` names that exact element. The
  element can be anywhere, and needs no id.
- Anything else is a CSS selector for the first matching element in the
  document, for example `"#app"`.

The element must exist before the call. Code inside the module (`EM_JS`,
`EM_ASM` or a JS library) can use `specialHTMLTargets` directly. Page scripts
need it exported, by linking with
`-sEXPORTED_RUNTIME_METHODS=specialHTMLTargets`:

```js
Module.specialHTMLTargets['!editor'] = document.querySelector('my-editor').shadowRoot.firstElementChild;
```

```c
gr_scene_host *host = gr_web_attach_to_element(scene, "!editor");
```

The `!` prefix is only a convention that keeps names apart from selectors.
Names stay registered until the application deletes them. The same names work
with Emscripten's html5 functions, such as
`emscripten_set_click_callback("!editor", ...)`. Under the names `"1"` and
`"2"`, `specialHTMLTargets` holds `document` and `window`; neither is an
element, so attaching to them fails.

## Tested environments

- Emscripten 7.0.0 (Homebrew) on macOS. Node 26 for the unit tests,
  Playwright 1.65 headless Chromium for `graphite.web`.
