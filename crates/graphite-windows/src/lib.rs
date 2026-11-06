//! Windows.UI.Composition backend for [graphite].
//!
//! Each graphite layer maps onto a small group of composition visuals (see
//! the `backend` module docs); drawings, images and shadow shapes are
//! rasterized with Direct2D into `CompositionDrawingSurface`s; shapes are
//! `ShapeVisual`s built from Direct2D path geometry. Commits are scheduled on
//! the thread's `DispatcherQueue`.
//!
//! A scene is presented through a [`SceneHost`]:
//! - [`attach_to_hwnd`] creates a child window filling a parent `HWND` and
//!   follows its DPI;
//! - [`attach_to_visual`] presents it in any `ContainerVisual` you own.
//!
//! [`offscreen`] renders a scene without showing it, for tests and
//! thumbnails.
//!
//! Everything here is bound to the thread that created the scene. That
//! thread needs a message loop; a `DispatcherQueue` is created for it on
//! first use if it has none. The process should be per-monitor DPI aware
//! (v2), or Windows scales the window's bitmap and blurs it.
#![cfg(windows)]

mod backend;
mod context;
mod convert;
mod draw;
mod effects;
mod host;
pub mod offscreen;
mod window;

pub use host::{SceneHost, attach_to_hwnd, attach_to_visual, compositor};
/// The `windows` bindings this crate is built against.
pub use windows;
