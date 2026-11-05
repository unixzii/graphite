//! Public layer handles.
//!
//! [`Layer`] is the untyped handle every layer can be viewed as; the typed
//! handles ([`ContentLayer`], [`ShapeLayer`], [`GradientLayer`]) wrap it and
//! `Deref` to it, so `parent.add_sublayer(&shape)` and `shape.set_opacity(..)`
//! just work.
//!
//! Handles are cheap: a weak pointer to the scene plus a slot key. Cloning a
//! handle retains the layer and dropping it releases it; a superlayer also
//! retains its sublayers, so a local handle can be dropped right after
//! `add_sublayer` without destroying the layer. All handles are bound to the
//! scene's thread (`!Send`).

use std::fmt;
use std::hash::{Hash, Hasher};
use std::mem;
use std::ops::Deref;
use std::rc::{Rc, Weak};

use crate::canvas::Canvas;
use crate::color::Color;
use crate::commit::{DirtyBits, LayerId};
use crate::geometry::{Affine, Point, Rect};
use crate::props::{Border, Contents, Corners, Gradient, Gravity, LayerKind, LayerProps, Shadow, ShapeProps};
use crate::resource::{FillRule, Paint, Path, Stroke};
use crate::scene::{ContentData, Drawer, KindData, LayerData, LayerKey, Parent, Scene, SceneInner};

/// Passed to a [`ContentLayer`]'s drawer.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct DrawInfo {
    /// The layer's bounds: the area to draw, in the layer's local space.
    pub bounds: Rect,
    /// Device pixels per point the result will be rasterized at. Drawings are
    /// recorded in points; this is informational (e.g. for hairlines).
    pub scale: f64,
}

/// A handle to any layer.
pub struct Layer {
    pub(crate) scene: Weak<SceneInner>,
    pub(crate) key: LayerKey,
}

/// Extra dirty bits implied by a change of `bounds.size`.
fn size_dependents(d: &LayerData) -> DirtyBits {
    let mut bits = DirtyBits::empty();
    if d.props.shadow.as_ref().is_some_and(|s| s.path.is_none()) {
        bits |= DirtyBits::SHADOW;
    }
    match &d.kind {
        KindData::Content(c) if matches!(c.contents, Contents::Image(_)) => bits |= DirtyBits::CONTENT,
        KindData::Gradient(_) => bits |= DirtyBits::CONTENT,
        _ => {}
    }
    bits
}

/// Extra dirty bits implied by a change of the corner shape.
fn corner_dependents(d: &LayerData) -> DirtyBits {
    if d.props.shadow.as_ref().is_some_and(|s| s.path.is_none()) { DirtyBits::SHADOW } else { DirtyBits::empty() }
}

impl Layer {
    /// Creates a plain container layer.
    pub fn new(scene: &Scene) -> Layer {
        SceneInner::create(&scene.inner, LayerKind::Container)
    }

    fn read<R: Default>(&self, f: impl FnOnce(&LayerData) -> R) -> R {
        self.scene.upgrade().and_then(|s| s.read(self.key, f)).unwrap_or_default()
    }

    /// Reads a common property; an inert handle reads the property defaults.
    fn read_props<R>(&self, f: impl FnOnce(&LayerProps) -> R) -> R {
        if let Some(scene) = self.scene.upgrade()
            && scene.contains(self.key)
        {
            return scene.read(self.key, |d| f(&d.props)).expect("layer is alive");
        }
        f(&LayerProps::default())
    }

    fn update<R>(&self, f: impl FnOnce(&mut LayerData) -> (DirtyBits, R)) -> Option<R> {
        let scene = self.scene.upgrade()?;
        scene.update(self.key, f)
    }

    /// Sets one common property; records `bits` plus `extra(d)` if it changed.
    fn set_common<T: PartialEq>(
        &self,
        value: T,
        bits: DirtyBits,
        extra: impl FnOnce(&LayerData) -> DirtyBits,
        field: impl FnOnce(&mut LayerProps) -> &mut T,
    ) {
        self.update(|d| {
            let extra = extra(d);
            let slot = field(&mut d.props);
            if *slot == value {
                return (DirtyBits::empty(), None);
            }
            (bits | extra, Some(mem::replace(slot, value)))
        });
    }

    fn props(&self) -> LayerProps {
        self.read_props(LayerProps::clone)
    }

    // ── identity ──

    /// The id backends see for this layer.
    pub fn id(&self) -> LayerId {
        self.key.id()
    }

    /// The layer's kind. Returns [`LayerKind::Container`] for an inert handle.
    pub fn kind(&self) -> LayerKind {
        self.scene.upgrade().and_then(|s| s.read(self.key, |d| d.kind.kind())).unwrap_or(LayerKind::Container)
    }

    /// `false` once the scene has been dropped.
    pub fn is_alive(&self) -> bool {
        self.scene.upgrade().is_some_and(|s| s.contains(self.key))
    }

    pub fn scene(&self) -> Option<Scene> {
        self.scene.upgrade().map(|inner| Scene { inner })
    }

    pub fn downgrade(&self) -> WeakLayer {
        WeakLayer { scene: self.scene.clone(), key: self.key }
    }

    /// Converts to a typed handle if the kind matches.
    pub fn downcast<T: TypedLayer>(self) -> Result<T, Layer> {
        if self.kind() == T::KIND { Ok(T::from_layer(self)) } else { Err(self) }
    }

    pub fn downcast_ref<T: TypedLayer>(&self) -> Option<&T> {
        if self.kind() != T::KIND {
            return None;
        }
        // SAFETY: `TypedLayer` is sealed and only implemented by the
        // `#[repr(transparent)]` wrappers around `Layer` defined below.
        Some(unsafe { &*(self as *const Layer as *const T) })
    }

    // ── tree ──

    /// Appends `child` as the topmost sublayer, removing it from its current
    /// superlayer first.
    ///
    /// # Panics
    /// If the layers belong to different scenes, if `child` is the root, a
    /// mask, or an ancestor of `self`.
    #[track_caller]
    pub fn add_sublayer(&self, child: &Layer) {
        self.insert_sublayer_at(child, None);
    }

    /// Inserts `child` at `index` among the sublayers (clamped to the end).
    /// Same panics as [`add_sublayer`](Self::add_sublayer).
    #[track_caller]
    pub fn insert_sublayer(&self, child: &Layer, index: usize) {
        self.insert_sublayer_at(child, Some(index));
    }

    #[track_caller]
    fn insert_sublayer_at(&self, child: &Layer, index: Option<usize>) {
        assert!(Weak::ptr_eq(&self.scene, &child.scene), "layers belong to different scenes");
        if let Some(scene) = self.scene.upgrade() {
            scene.insert_sublayer(self.key, child.key, index);
        }
    }

    /// Detaches the layer from its superlayer (or from the layer it masks).
    /// The layer survives as long as handles to it exist.
    pub fn remove_from_superlayer(&self) {
        if let Some(scene) = self.scene.upgrade() {
            scene.remove_from_parent(self.key);
        }
    }

    pub fn sublayers(&self) -> Vec<Layer> {
        let Some(scene) = self.scene.upgrade() else { return Vec::new() };
        let keys = scene.read(self.key, |d| d.children.clone()).unwrap_or_default();
        keys.into_iter().map(|k| SceneInner::handle(&scene, k)).collect()
    }

    pub fn superlayer(&self) -> Option<Layer> {
        let scene = self.scene.upgrade()?;
        let parent = scene.read(self.key, |d| match d.parent {
            Some(Parent::Sublayer(p)) => Some(p),
            _ => None,
        })??;
        Some(SceneInner::handle(&scene, parent))
    }

    /// Masks this layer (and its subtree) by the alpha of `mask`. The mask
    /// layer must not be in the tree, must not have a mask itself, and lives
    /// in this layer's local space.
    #[track_caller]
    pub fn set_mask(&self, mask: Option<&Layer>) {
        if let Some(m) = mask {
            assert!(Weak::ptr_eq(&self.scene, &m.scene), "layers belong to different scenes");
        }
        if let Some(scene) = self.scene.upgrade() {
            scene.set_mask(self.key, mask.map(|m| m.key));
        }
    }

    pub fn mask(&self) -> Option<Layer> {
        let scene = self.scene.upgrade()?;
        let mask = scene.read(self.key, |d| d.mask)??;
        Some(SceneInner::handle(&scene, mask))
    }

    // ── geometry ──

    pub fn bounds(&self) -> Rect {
        self.read_props(|p| p.bounds)
    }

    pub fn set_bounds(&self, bounds: Rect) {
        self.update(|d| {
            let old = d.props.bounds;
            if old == bounds {
                return (DirtyBits::empty(), ());
            }
            let extra = if old.size != bounds.size { size_dependents(d) } else { DirtyBits::empty() };
            d.props.bounds = bounds;
            (DirtyBits::GEOMETRY | extra, ())
        });
    }

    pub fn position(&self) -> Point {
        self.read_props(|p| p.position)
    }

    pub fn set_position(&self, position: Point) {
        self.set_common(position, DirtyBits::GEOMETRY, |_| DirtyBits::empty(), |p| &mut p.position);
    }

    pub fn anchor_point(&self) -> Point {
        self.read_props(|p| p.anchor_point)
    }

    pub fn set_anchor_point(&self, anchor: Point) {
        self.set_common(anchor, DirtyBits::GEOMETRY, |_| DirtyBits::empty(), |p| &mut p.anchor_point);
    }

    pub fn transform(&self) -> Affine {
        self.read_props(|p| p.transform)
    }

    pub fn set_transform(&self, transform: Affine) {
        self.set_common(transform, DirtyBits::GEOMETRY, |_| DirtyBits::empty(), |p| &mut p.transform);
    }

    /// The bounding box of the layer in its superlayer's local space.
    pub fn frame(&self) -> Rect {
        self.props().frame()
    }

    /// Sets `position` and `bounds.size` so that the layer covers `frame`,
    /// keeping `bounds.origin`. Like `CALayer`, assumes an identity transform.
    pub fn set_frame(&self, frame: Rect) {
        self.update(|d| {
            let p = &mut d.props;
            let position = Point::new(
                frame.origin.x + p.anchor_point.x * frame.size.width,
                frame.origin.y + p.anchor_point.y * frame.size.height,
            );
            let size_changed = p.bounds.size != frame.size;
            if p.position == position && !size_changed {
                return (DirtyBits::empty(), ());
            }
            p.position = position;
            p.bounds.size = frame.size;
            let extra = if size_changed { size_dependents(d) } else { DirtyBits::empty() };
            (DirtyBits::GEOMETRY | extra, ())
        });
    }

    // ── appearance ──

    pub fn opacity(&self) -> f32 {
        self.read_props(|p| p.opacity)
    }

    pub fn set_opacity(&self, opacity: f32) {
        self.set_common(opacity.clamp(0.0, 1.0), DirtyBits::APPEARANCE, |_| DirtyBits::empty(), |p| &mut p.opacity);
    }

    pub fn is_hidden(&self) -> bool {
        self.read_props(|p| p.hidden)
    }

    pub fn set_hidden(&self, hidden: bool) {
        self.set_common(hidden, DirtyBits::APPEARANCE, |_| DirtyBits::empty(), |p| &mut p.hidden);
    }

    pub fn masks_to_bounds(&self) -> bool {
        self.read_props(|p| p.masks_to_bounds)
    }

    pub fn set_masks_to_bounds(&self, masks: bool) {
        self.set_common(masks, DirtyBits::APPEARANCE, |_| DirtyBits::empty(), |p| &mut p.masks_to_bounds);
    }

    pub fn background(&self) -> Option<Color> {
        self.read_props(|p| p.background)
    }

    pub fn set_background(&self, color: Option<Color>) {
        self.set_common(color, DirtyBits::APPEARANCE, |_| DirtyBits::empty(), |p| &mut p.background);
    }

    pub fn corner_radius(&self) -> f64 {
        self.read_props(|p| p.corner_radius)
    }

    pub fn set_corner_radius(&self, radius: f64) {
        self.set_common(radius.max(0.0), DirtyBits::APPEARANCE, corner_dependents, |p| &mut p.corner_radius);
    }

    pub fn masked_corners(&self) -> Corners {
        self.read_props(|p| p.masked_corners)
    }

    pub fn set_masked_corners(&self, corners: Corners) {
        self.set_common(corners, DirtyBits::APPEARANCE, corner_dependents, |p| &mut p.masked_corners);
    }

    pub fn border(&self) -> Option<Border> {
        self.read_props(|p| p.border)
    }

    pub fn set_border(&self, border: Option<Border>) {
        self.set_common(border, DirtyBits::APPEARANCE, |_| DirtyBits::empty(), |p| &mut p.border);
    }

    pub fn shadow(&self) -> Option<Shadow> {
        self.read_props(|p| p.shadow.clone())
    }

    pub fn set_shadow(&self, shadow: Option<Shadow>) {
        self.set_common(shadow, DirtyBits::SHADOW, |_| DirtyBits::empty(), |p| &mut p.shadow);
    }
}

impl Clone for Layer {
    fn clone(&self) -> Layer {
        if let Some(scene) = self.scene.upgrade() {
            scene.retain(self.key);
        }
        Layer { scene: self.scene.clone(), key: self.key }
    }
}

impl Drop for Layer {
    fn drop(&mut self) {
        if let Some(scene) = self.scene.upgrade() {
            scene.release(self.key);
        }
    }
}

impl PartialEq for Layer {
    fn eq(&self, other: &Layer) -> bool {
        self.key == other.key && Weak::ptr_eq(&self.scene, &other.scene)
    }
}

impl Eq for Layer {}

impl Hash for Layer {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.scene.as_ptr().hash(state);
        self.key.hash(state);
    }
}

impl fmt::Debug for Layer {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Layer").field("id", &self.id()).field("kind", &self.kind()).finish()
    }
}

/// A non-owning layer handle; use it to refer to a layer from its own
/// drawer (or anything the layer owns) without creating a reference cycle.
#[derive(Clone)]
pub struct WeakLayer {
    scene: Weak<SceneInner>,
    key: LayerKey,
}

impl WeakLayer {
    pub fn upgrade(&self) -> Option<Layer> {
        let scene = self.scene.upgrade()?;
        scene.contains(self.key).then(|| SceneInner::handle(&scene, self.key))
    }
}

impl fmt::Debug for WeakLayer {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("WeakLayer").field("id", &self.key.id()).finish()
    }
}

// ───────────────────────────── typed handles ─────────────────────────────

mod sealed {
    pub trait Sealed {}
}

/// Implemented by the typed layer handles. Sealed.
pub trait TypedLayer: sealed::Sealed + Deref<Target = Layer> + Sized {
    const KIND: LayerKind;
    #[doc(hidden)]
    fn from_layer(layer: Layer) -> Self;
}

macro_rules! typed_layer {
    ($(#[$meta:meta])* $name:ident => $kind:expr) => {
        $(#[$meta])*
        #[derive(Clone, PartialEq, Eq, Hash)]
        #[repr(transparent)]
        pub struct $name(Layer);

        impl $name {
            pub fn new(scene: &Scene) -> $name {
                $name(SceneInner::create(&scene.inner, $kind))
            }
        }

        impl Deref for $name {
            type Target = Layer;
            fn deref(&self) -> &Layer {
                &self.0
            }
        }

        impl AsRef<Layer> for $name {
            fn as_ref(&self) -> &Layer {
                &self.0
            }
        }

        impl From<$name> for Layer {
            fn from(layer: $name) -> Layer {
                layer.0
            }
        }

        impl sealed::Sealed for $name {}

        impl TypedLayer for $name {
            const KIND: LayerKind = $kind;
            fn from_layer(layer: Layer) -> $name {
                $name(layer)
            }
        }

        impl fmt::Debug for $name {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.debug_tuple(stringify!($name)).field(&self.0.id()).finish()
            }
        }
    };
}

typed_layer! {
    /// A layer with bitmap or drawn contents.
    ContentLayer => LayerKind::Content
}

typed_layer! {
    /// A layer that fills and/or strokes a vector path.
    ShapeLayer => LayerKind::Shape
}

typed_layer! {
    /// A layer filled with a gradient.
    GradientLayer => LayerKind::Gradient
}

// The typed handle guarantees the kind, so the other arms are unreachable.
macro_rules! kind_data {
    ($d:expr, $variant:ident) => {
        match &mut $d.kind {
            KindData::$variant(x) => x,
            _ => unreachable!("typed layer handle with mismatched kind"),
        }
    };
}

impl ContentLayer {
    fn update_content<R>(&self, f: impl FnOnce(&mut ContentData) -> (DirtyBits, R)) -> Option<R> {
        self.update(|d| f(kind_data!(d, Content)))
    }

    fn read_content<R: Default>(&self, f: impl FnOnce(&ContentData) -> R) -> R {
        self.read(|d| match &d.kind {
            KindData::Content(c) => f(c),
            _ => unreachable!("typed layer handle with mismatched kind"),
        })
    }

    pub fn contents(&self) -> Contents {
        self.read_content(|c| c.contents.clone())
    }

    /// Sets the contents directly. Removes the drawer, if any.
    pub fn set_contents(&self, contents: Contents) {
        let _old = self.update_content(|c| {
            if c.contents == contents && c.drawer.is_none() {
                return (DirtyBits::empty(), None);
            }
            c.needs_display = false;
            let old = (mem::replace(&mut c.contents, contents), c.drawer.take());
            (DirtyBits::CONTENT, Some(old))
        });
        // `_old` (possibly a drawer closure) is dropped here, outside any borrow.
    }

    pub fn gravity(&self) -> Gravity {
        self.read_content(|c| c.gravity)
    }

    pub fn set_gravity(&self, gravity: Gravity) {
        self.update_content(|c| {
            if c.gravity == gravity {
                return (DirtyBits::empty(), ());
            }
            c.gravity = gravity;
            (DirtyBits::CONTENT, ())
        });
    }

    /// Installs a drawer and schedules it to run at the next commit. The
    /// drawer records into a [`Canvas`] in the layer's local space and runs
    /// again after every [`set_needs_display`](Self::set_needs_display).
    ///
    /// Capture a [`WeakLayer`], not a [`Layer`], if the drawer needs to refer
    /// to its own layer or an ancestor; a strong handle would keep the layer
    /// alive until the scene is dropped.
    pub fn set_drawer(&self, drawer: impl Fn(&mut Canvas, &DrawInfo) + 'static) {
        let drawer: Drawer = Rc::new(drawer);
        let _old = self.update_content(|c| {
            c.needs_display = true;
            (DirtyBits::CONTENT, c.drawer.replace(drawer))
        });
    }

    /// Removes the drawer; the last drawing stays as the contents.
    pub fn clear_drawer(&self) {
        let _old = self.update_content(|c| (DirtyBits::empty(), c.drawer.take()));
    }

    pub fn has_drawer(&self) -> bool {
        self.read_content(|c| c.drawer.is_some())
    }

    /// Asks the drawer to run again at the next commit. No-op without a drawer.
    pub fn set_needs_display(&self) {
        self.update_content(|c| {
            if c.drawer.is_none() || c.needs_display {
                return (DirtyBits::empty(), ());
            }
            c.needs_display = true;
            (DirtyBits::CONTENT, ())
        });
    }
}

impl ShapeLayer {
    fn set_shape<T: PartialEq>(&self, value: T, field: impl FnOnce(&mut ShapeProps) -> &mut T) {
        self.update(|d| {
            let slot = field(kind_data!(d, Shape));
            if *slot == value {
                return (DirtyBits::empty(), None);
            }
            (DirtyBits::CONTENT, Some(mem::replace(slot, value)))
        });
    }

    /// All shape properties.
    pub fn shape(&self) -> ShapeProps {
        self.read(|d| match &d.kind {
            KindData::Shape(s) => s.clone(),
            _ => unreachable!("typed layer handle with mismatched kind"),
        })
    }

    pub fn path(&self) -> Option<Path> {
        self.shape().path
    }

    /// The path, in the layer's local space.
    pub fn set_path(&self, path: Option<Path>) {
        self.set_shape(path, |s| &mut s.path);
    }

    pub fn set_fill(&self, fill: Option<Paint>) {
        self.set_shape(fill, |s| &mut s.fill);
    }

    pub fn set_fill_rule(&self, rule: FillRule) {
        self.set_shape(rule, |s| &mut s.fill_rule);
    }

    pub fn set_stroke(&self, stroke: Option<Stroke>) {
        self.set_shape(stroke, |s| &mut s.stroke);
    }

    /// Start of the stroked portion, as a fraction of the path length.
    pub fn set_stroke_start(&self, start: f64) {
        self.set_shape(start.clamp(0.0, 1.0), |s| &mut s.stroke_start);
    }

    /// End of the stroked portion, as a fraction of the path length.
    pub fn set_stroke_end(&self, end: f64) {
        self.set_shape(end.clamp(0.0, 1.0), |s| &mut s.stroke_end);
    }
}

impl GradientLayer {
    pub fn gradient(&self) -> Option<Gradient> {
        self.read(|d| match &d.kind {
            KindData::Gradient(g) => g.clone(),
            _ => unreachable!("typed layer handle with mismatched kind"),
        })
    }

    pub fn set_gradient(&self, gradient: Option<Gradient>) {
        self.update(|d| {
            let slot = kind_data!(d, Gradient);
            if *slot == gradient {
                return (DirtyBits::empty(), None);
            }
            (DirtyBits::CONTENT, Some(mem::replace(slot, gradient)))
        });
    }
}
