# Graphite documentation

Start here if you are new to the project.

| Document | Read it to… |
|---|---|
| [Architecture](architecture.md) | understand the model → commit → backend pipeline, ownership, the `CommitBatch` contract, coordinate spaces and pixel snapping |
| [Layer model and API semantics](layer-model.md) | learn what the public API means, especially where it differs from `CALayer` |
| [Testing](testing.md) | run the tests, understand the conformance tiers, and add cases |
| [Writing a backend](backends/writing-a-backend.md) | add a platform (GTK, Windows, Web) |
| [macOS backend](backends/macos.md) | see how Core Animation is mapped, and the pitfalls found while doing it |
| [GTK backend](backends/gtk.md) | see how GSK render nodes are cached and composed, how commits follow the frame clock, and how to test on Linux |
| [Platform notes](platform-notes.md) | check verified facts about each platform's rendering stack and the capability matrix |
| [Status and roadmap](roadmap.md) | see what is done, what is next, and the open questions |

API reference: `cargo doc --workspace --no-deps --open`.

## Conventions for contributors

- Rust 2024 edition, MSRV 1.88; `cargo fmt` (120-column max width, see
  `rustfmt.toml`) and `cargo clippy --workspace --all-targets` must be clean.
- Model behavior gets a test in `crates/graphite/tests/scene.rs`. Anything
  a backend renders gets a conformance case, and the case must be seen
  failing when the behavior is broken.
- Platform facts that influence the design belong in
  [platform notes](platform-notes.md), together with how they were verified.
