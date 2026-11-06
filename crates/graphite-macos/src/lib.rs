//! Core Animation backend for [graphite].
//!
//! Each graphite layer is mapped onto native `CALayer`s (see the
//! `backend` module docs for the exact structure); drawings are replayed
//! into CoreGraphics bitmaps; commits are scheduled on the current run loop
//! right before Core Animation's own commit.
//!
//! A scene is presented through a [`SceneHost`]:
//! - [`attach_to_view`] creates a layer-hosting `NSView` that fills a parent
//!   view and follows its window's backing scale;
//! - [`attach_to_layer`] presents it in any `CALayer` you own.
//!
//! [`offscreen::render`] rasterizes a layer tree without a window, for tests
//! and thumbnails.
#![cfg(target_os = "macos")]

mod backend;
mod convert;
mod draw;
mod host;
pub mod offscreen;
mod view;

pub use host::{SceneHost, attach_to_layer, attach_to_view};
