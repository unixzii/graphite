//! The commit batch: what Core hands to a backend once per frame.

use bitflags::bitflags;

use crate::canvas::DisplayList;
use crate::geometry::{Affine, Rect, Size, TransformKind, Vec2};
use crate::props::{Border, Contents, Corners, Gradient, Gravity, LayerKind, LayerProps, ShapeProps};
use crate::resource::{Image, Path, PathEl};

/// Stable identity of a layer, as seen by backends. Never reused within a
/// scene (the slot generation is part of the id).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct LayerId(pub(crate) u64);

impl LayerId {
    pub fn get(self) -> u64 {
        self.0
    }
}

bitflags! {
    /// Which groups of properties changed since the layer was last committed.
    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
    pub struct DirtyBits: u16 {
        /// bounds, position, anchor_point, transform
        const GEOMETRY = 1 << 0;
        /// opacity, hidden, masks_to_bounds, background, corner radius, border
        const APPEARANCE = 1 << 1;
        const SHADOW = 1 << 2;
        /// Kind-specific properties (contents, shape, gradient).
        const CONTENT = 1 << 3;
        const MASK = 1 << 4;
        /// The ordered list of sublayers.
        const CHILDREN = 1 << 5;
    }
}

/// Everything that changed in a scene since the previous commit.
///
/// Backends apply it in this order:
/// 1. create native objects for [`realized`](Self::realized);
/// 2. apply [`children`](Self::children) in two passes (see below);
/// 3. apply [`updates`](Self::updates);
/// 4. destroy native objects for [`released`](Self::released).
///
/// `children` carries the complete, ordered sublayer list of every layer
/// whose list changed. A layer can move between parents in one commit, so
/// backends first detach, for every listed parent, the native children that
/// are no longer in its list *and whose current native parent is still that
/// parent*; then they install each new list, re-parenting as needed. A layer
/// may also be listed under a new parent while still natively attached to a
/// parent released in this or an earlier batch.
#[derive(Debug, Default)]
pub struct CommitBatch {
    /// Set on the first commit only: the scene's root layer.
    pub root: Option<LayerId>,
    /// Device pixels per point to rasterize at. When it changes, every
    /// non-container layer comes with [`DirtyBits::CONTENT`].
    pub backing_scale: f64,
    pub realized: Vec<(LayerId, LayerKind)>,
    pub children: Vec<(LayerId, Vec<LayerId>)>,
    pub updates: Vec<LayerUpdate>,
    pub released: Vec<LayerId>,
}

impl CommitBatch {
    pub fn is_empty(&self) -> bool {
        self.root.is_none()
            && self.realized.is_empty()
            && self.children.is_empty()
            && self.updates.is_empty()
            && self.released.is_empty()
    }
}

/// A full snapshot of one layer's properties plus which groups changed.
#[derive(Clone, Debug)]
pub struct LayerUpdate {
    pub id: LayerId,
    /// Never contains [`DirtyBits::CHILDREN`]; that goes to [`CommitBatch::children`].
    pub changed: DirtyBits,
    pub layer: Resolved,
}

/// A layer's properties together with values derived by Core, so that every
/// backend interprets the CALayer model identically.
///
/// Two coordinate spaces matter:
/// - **box space**: `(0, 0)` is the layer's top-left corner, `(w, h)` its
///   bottom-right — where background, border, image contents and the clip live;
/// - **local space**: `box space - bounds_offset`, i.e. offset by
///   `bounds.origin` — where sublayers, display lists and paths live.
#[derive(Clone, Debug)]
pub struct Resolved {
    /// The raw properties as set by the user (CA-like backends use these).
    pub props: LayerProps,
    /// Maps box space to the superlayer's local space:
    /// `T(position) · transform · T(-anchor_point · size)`, with its
    /// translation snapped to device pixels when pixel snapping is on.
    /// Identity for the root.
    pub transform: Affine,
    /// What snapping added to `transform`'s translation, in the superlayer's
    /// local space. Backends that build geometry from the raw properties
    /// (Core Animation) add it to `position`.
    pub snap_offset: Vec2,
    /// `-bounds.origin` (snapped when pixel snapping is on): translate
    /// local-space content by this to reach box space.
    pub bounds_offset: Vec2,
    /// The box size to draw: `bounds.size`, snapped so that the far edges
    /// land on device pixels too when pixel snapping is on. Use it instead
    /// of `props.bounds.size`.
    pub size: Size,
    /// `props.border` with its width rounded to whole device pixels (at
    /// least one) when pixel snapping is on. Use it instead of
    /// `props.border`.
    pub border: Option<Border>,
    pub mask: Option<LayerId>,
    /// Present iff `props.shadow` is.
    pub shadow_shape: Option<ShadowShape>,
    pub content: ResolvedContent,
}

/// The shape that casts a layer's shadow.
#[derive(Clone, Debug, PartialEq)]
pub enum ShadowShape {
    /// The layer's rounded box (`rect`, in box space) grown by `spread`.
    /// Backends with a native spread (GSK's outset shadow) use the fields;
    /// others use [`ShadowShape::path`].
    RoundedRect { rect: Rect, radius: f64, corners: Corners, spread: f64 },
    /// In local space (the user-provided `Shadow::path`).
    Path(Path),
}

impl ShadowShape {
    /// The final shadow outline in box space (`bounds_offset` moves a custom
    /// path out of local space).
    pub fn path(&self, bounds_offset: Vec2) -> Path {
        match self {
            ShadowShape::RoundedRect { rect, radius, corners, spread } => {
                let (rect, radius) = grow_rounded_rect(*rect, *radius, *spread);
                Path::rounded_rect(rect, radius, *corners)
            }
            ShadowShape::Path(path) => Path::from_elements(
                path.elements()
                    .iter()
                    .map(|el| match *el {
                        PathEl::MoveTo(p) => PathEl::MoveTo(p + bounds_offset),
                        PathEl::LineTo(p) => PathEl::LineTo(p + bounds_offset),
                        PathEl::QuadTo(c, p) => PathEl::QuadTo(c + bounds_offset, p + bounds_offset),
                        PathEl::CurveTo(c1, c2, p) => {
                            PathEl::CurveTo(c1 + bounds_offset, c2 + bounds_offset, p + bounds_offset)
                        }
                        PathEl::Close => PathEl::Close,
                    })
                    .collect::<Vec<_>>(),
            ),
        }
    }
}

/// `rect` grown by `spread` on every side (never below zero size); a
/// non-zero radius grows with it (never below zero), a zero radius stays
/// square. This matches GSK's outset shadows.
pub fn grow_rounded_rect(rect: Rect, radius: f64, spread: f64) -> (Rect, f64) {
    let w = (rect.width() + 2.0 * spread).max(0.0);
    let h = (rect.height() + 2.0 * spread).max(0.0);
    let cx = rect.min_x() + rect.width() / 2.0;
    let cy = rect.min_y() + rect.height() / 2.0;
    let radius = if radius > 0.0 { (radius + spread).max(0.0) } else { 0.0 };
    (Rect::new(cx - w / 2.0, cy - h / 2.0, w, h), radius)
}

/// Kind-specific contents, resolved.
#[derive(Clone, Debug, PartialEq)]
pub enum ResolvedContent {
    None,
    /// `rect` is in box space, already placed according to the layer's gravity.
    Image {
        image: Image,
        rect: Rect,
    },
    /// In local space.
    Drawing(DisplayList),
    Shape(ShapeProps),
    Gradient(Gradient),
}

/// Rounds `v` to the device-pixel grid of `scale`.
fn snap(v: f64, scale: f64) -> f64 {
    (v * scale).round() / scale
}

/// Rounds a positive length to whole device pixels, keeping at least one.
fn snap_length(v: f64, scale: f64) -> f64 {
    if v <= 0.0 { v } else { (v * scale).round().max(1.0) / scale }
}

/// `snap_scale` is the backing scale when pixel snapping is on.
///
/// Snapping is local: each layer's translation and bounds offset land on
/// the device-pixel grid, and so does its size when its own transform is a
/// translation (so all four edges are crisp). Under translation-only
/// ancestors that makes every layer's edges pixel-aligned without having to
/// re-resolve descendants when an ancestor moves. Border widths become whole
/// device pixels.
pub(crate) fn resolve(
    props: &LayerProps,
    contents: ResolvedContentSource<'_>,
    mask: Option<LayerId>,
    is_root: bool,
    snap_scale: Option<f64>,
) -> Resolved {
    let snap_scale = snap_scale.filter(|s| *s > 0.0);
    let axis_aligned = props.transform.kind() <= TransformKind::Translate;
    let size = match snap_scale {
        Some(s) if axis_aligned => {
            Size::new(snap_length(props.bounds.width(), s), snap_length(props.bounds.height(), s))
        }
        _ => props.bounds.size,
    };
    let border = match (props.border, snap_scale) {
        (Some(b), Some(s)) => Some(Border { width: snap_length(b.width, s), ..b }),
        (border, _) => border,
    };
    let mut transform = if is_root { Affine::IDENTITY } else { props.box_to_parent_with_size(size) };
    let mut snap_offset = Vec2::ZERO;
    let mut bounds_offset = -props.bounds.origin.to_vec2();
    if let Some(s) = snap_scale {
        let (tx, ty) = (snap(transform.tx, s), snap(transform.ty, s));
        snap_offset = Vec2::new(tx - transform.tx, ty - transform.ty);
        transform.tx = tx;
        transform.ty = ty;
        bounds_offset = Vec2::new(snap(bounds_offset.x, s), snap(bounds_offset.y, s));
    }
    let shadow_shape = props.shadow.as_ref().map(|s| match &s.path {
        Some(p) => ShadowShape::Path(p.clone()),
        None => ShadowShape::RoundedRect {
            rect: Rect::from_size(size),
            radius: props.corner_radius,
            corners: props.masked_corners,
            spread: s.spread,
        },
    });
    let content = match contents {
        ResolvedContentSource::None => ResolvedContent::None,
        ResolvedContentSource::Contents(Contents::None, _) => ResolvedContent::None,
        ResolvedContentSource::Contents(Contents::Image(image), gravity) => {
            ResolvedContent::Image { image: image.clone(), rect: gravity.place(image.size(), size) }
        }
        ResolvedContentSource::Contents(Contents::Drawing(dl), _) => ResolvedContent::Drawing(dl.clone()),
        ResolvedContentSource::Shape(s) => ResolvedContent::Shape(s.clone()),
        ResolvedContentSource::Gradient(Some(g)) => ResolvedContent::Gradient(g.clone()),
        ResolvedContentSource::Gradient(None) => ResolvedContent::None,
    };
    Resolved { props: props.clone(), transform, snap_offset, bounds_offset, size, border, mask, shadow_shape, content }
}

pub(crate) enum ResolvedContentSource<'a> {
    None,
    Contents(&'a Contents, Gravity),
    Shape(&'a ShapeProps),
    Gradient(Option<&'a Gradient>),
}
