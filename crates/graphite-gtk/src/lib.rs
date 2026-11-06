//! GTK 4 / GSK backend for [graphite].
//!
//! GSK has no retained layer tree, so this backend keeps one cached
//! `GskRenderNode` per layer (see the `tree` module docs for how a layer is
//! composed) and rebuilds only what a commit touched. Drawings are
//! translated into GPU-rendered GSK nodes (fill, stroke, gradient, texture,
//! clip, mask); nothing goes through Cairo.
//!
//! A scene is presented through a [`SceneHost`]:
//! - [`attach`] creates a widget to put anywhere in a GTK widget tree.
//!   Commits run in the frame clock's layout phase, after tick callbacks;
//!   the scene follows the surface's fractional scale.
//! - [`attach_offscreen`] has no widget; [`offscreen::render`] renders it
//!   with a chosen GSK renderer, for tests and thumbnails.
//!
//! Everything here is bound to GTK's main thread.
#![cfg(target_os = "linux")]

mod backend;
mod convert;
mod draw;
mod host;
pub mod offscreen;
mod tree;
mod widget;

/// The GTK bindings this crate is built against.
pub use gtk;
pub use host::{SceneHost, attach, attach_offscreen};
pub use widget::GraphiteWidget;
