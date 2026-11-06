# graphite

A cross-platform, retained-mode 2D layer library with a `CALayer`-style API
and a CGContext-style drawing API, rendered by each platform's native
compositor (Core Animation on macOS and GSK on Linux today; Windows and Web
planned).

```sh
cargo run --example demo     # open the demo window (macOS, or Linux with GTK 4.22+)
cargo test --workspace
```

Documentation for contributors is in [docs/](docs/README.md).
