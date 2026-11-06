# graphite

A cross-platform, retained-mode 2D layer library with a `CALayer`-style API
and a CGContext-style drawing API, rendered by each platform's native compositor.
The C migration includes the core, macOS and Windows backends, and optional
GTK 4.22 support (`-DGRAPHITE_GTK=ON`). The Rust reference
implementation retains its macOS, GTK and Windows backends; Web is planned.

Build and run the C demo on macOS:

```sh
cmake -S . -B /tmp/graphite-c -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/graphite-c --target graphite_demo
/tmp/graphite-c/examples/demo/demo                  # feature showcase
/tmp/graphite-c/examples/demo/demo --scene ui       # typical application window
/tmp/graphite-c/examples/demo/demo --selftest       # verify 60 animation ticks
```

On Windows 11, build from a Visual Studio developer prompt; the same target
produces `examples/demo/demo.exe` with compositor-clock animation.

For Linux, configure with `-DGRAPHITE_GTK=ON` (GTK 4.22+); the same demo target
uses GTK frame-clock updates. `GSK_RENDERER=gl` or `cairo` selects the renderer.

The original Rust demo is preserved in `examples/demo-rs`:

```sh
cargo run --example demo-rs
cargo run --example demo-rs -- --scene ui
cargo test --workspace
```

See the [demo guide](examples/demo/README.md), [C build and tests](src/README.md),
and [contributor documentation](docs/README.md).

For static/shared installation, build switches and `find_package` integration,
see the [CMake package guide](docs/cmake.md).
