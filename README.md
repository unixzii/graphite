# graphite

A cross-platform, retained-mode 2D layer library with a `CALayer`-style API
and a CGContext-style drawing API, rendered by each platform's native compositor.
The C migration currently includes the core and macOS backend. The Rust reference
implementation retains its macOS, GTK and Windows backends; Web is planned.

Build and run the C demo on macOS:

```sh
cmake -S . -B /tmp/graphite-c -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/graphite-c --target graphite_demo
/tmp/graphite-c/examples/demo/demo                  # feature showcase
/tmp/graphite-c/examples/demo/demo --scene ui       # typical application window
/tmp/graphite-c/examples/demo/demo --selftest       # verify 60 animation ticks
```

The original Rust demo is preserved in `examples/demo-rs`:

```sh
cargo run --example demo-rs
cargo run --example demo-rs -- --scene ui
cargo test --workspace
```

See the [demo guide](examples/demo/README.md), [C build and tests](src/README.md),
and [contributor documentation](docs/README.md).
