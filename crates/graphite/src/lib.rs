//! Graphite: a cross-platform, retained-mode 2D layer library modelled on
//! `CALayer`, rendered by each platform's native compositor.
//!
//! # Architecture
//!
//! ```text
//! user ─► build layer model (Scene + LayerData, platform independent)
//!                          │  once per frame: Scene::commit()
//!                          ▼
//!                        phase 1: run drawers → DisplayList
//!                        phase 2: dirty layers → CommitBatch (with derived values)
//!                          │
//!                          ▼
//!                        Backend::apply  (Core Animation / Windows.UI.Composition /
//!                                         GSK render nodes / DOM)
//! ```
//!
//! Layers are mutable model objects owned by their [`Scene`]; resources
//! ([`Path`], [`Image`], [`DisplayList`], …) are immutable and shareable.
//!
//! ```
//! use graphite::*;
//!
//! let scene = Scene::new(Size::new(400.0, 300.0));
//!
//! let card = Layer::new(&scene);
//! card.set_frame(Rect::new(20.0, 20.0, 240.0, 120.0));
//! card.set_background(Some(Color::WHITE));
//! card.set_corner_radius(12.0);
//! card.set_shadow(Some(Shadow::new(Color::BLACK.with_alpha(0.2), Vec2::new(0.0, 4.0), 8.0)));
//!
//! let ring = ShapeLayer::new(&scene);
//! ring.set_frame(Rect::new(16.0, 16.0, 40.0, 40.0));
//! ring.set_path(Some(Path::ellipse(Rect::new(0.0, 0.0, 40.0, 40.0))));
//! ring.set_stroke(Some(Stroke::new(Color::rgb(0.2, 0.4, 1.0), 2.0)));
//!
//! card.add_sublayer(&ring); // typed handles deref to `Layer`
//! scene.root().add_sublayer(&card);
//! // `card` and `ring` can be dropped now: the tree keeps them alive.
//! ```

mod backend;
mod canvas;
mod color;
mod commit;
mod geometry;
mod layer;
mod props;
mod resource;
mod scene;

pub mod testing;

pub use backend::Backend;
pub use canvas::{Canvas, DisplayList, DrawOp};
pub use color::Color;
pub use commit::{CommitBatch, DirtyBits, LayerId, LayerUpdate, Resolved, ResolvedContent, ShadowShape};
pub use geometry::{Affine, Point, Rect, Size, TransformKind, Vec2};
pub use layer::{ContentLayer, DrawInfo, GradientLayer, Layer, ShapeLayer, TypedLayer, WeakLayer};
pub use props::{Border, Contents, Corners, Gradient, Gravity, LayerKind, LayerProps, Shadow, ShapeProps};
pub use resource::{
    FillRule, GradientStop, Image, LineCap, LineJoin, Paint, Path, PathBuilder, PathEl, ResourceId, Stroke, StrokeStyle,
};
pub use scene::{Scene, WeakScene};
