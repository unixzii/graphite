//! Maps commit batches onto a Windows.UI.Composition visual tree.
//!
//! Every graphite layer becomes a small group of visuals (coordinates in
//! points; the host scales the whole tree by the backing scale):
//!
//! ```text
//! outer (ContainerVisual)        TransformMatrix = resolved transform (box space → parent's local space),
//!   │                            Opacity (group opacity), IsVisible
//!   ├─ shadow (SpriteVisual)     no brush; DropShadow whose Mask is the rasterized shadow shape
//!   └─ [mask (LayerVisual)]      only when masked: alpha-mask effect fed by a VisualSurface of the mask
//!        └─ body (ContainerVisual)
//!             ├─ clipped (ContainerVisual)     Clip = rounded box (masks_to_bounds)
//!             │    ├─ background (SpriteVisual)    color brush, rounded by a RectangleClip
//!             │    ├─ content                      SpriteVisual (image, drawing, gradient) | ShapeVisual
//!             │    └─ sublayers (ContainerVisual)  Offset = bounds_offset (local space); the sublayers' outers
//!             └─ border (ShapeVisual)          an even-odd ring, inside the box
//! ```
//!
//! The shadow sits beside the body, so the clip does not cut it (graphite's
//! semantics), and it is a sibling below the body, so it shows through a
//! layer without a background. A `ContainerVisual`'s opacity is already
//! group opacity in composition; no `LayerVisual` is needed for it.
//!
//! The border is outside the clip, and under an opaque border the
//! background and the clip stop at the border's midline. Otherwise their
//! anti-aliased edges coincide with the border's outer edge, and the
//! background or contents bleed through it as a dark fringe (and the clip
//! would anti-alias the border's edge a second time).

use std::collections::HashMap;
use std::rc::Rc;

use graphite::{
    Affine, Backend, CommitBatch, Corners, DirtyBits, FillRule, Gradient, GradientStop, LayerId, LayerKind,
    LayerUpdate, Paint, Path, Rect, Resolved, ResolvedContent, ResourceId, ShadowShape, ShapeProps, Vec2, WeakScene,
};
use windows::UI::Composition::{
    CompositionBitmapInterpolationMode, CompositionBrush, CompositionClip, CompositionColorSpace,
    CompositionContainerShape, CompositionDrawingSurface, CompositionGradientBrush, CompositionGradientExtendMode,
    CompositionMappingMode, CompositionSpriteShape, CompositionStretch, CompositionStrokeCap,
    CompositionStrokeLineJoin, CompositionVisualSurface, Compositor, ContainerVisual, DropShadow, LayerVisual,
    ShapeVisual, SpriteVisual, Visual,
};
use windows::core::{HSTRING, Interface, Result};
use windows_numerics::Vector3;

use crate::context::{Context, schedule_commit};
use crate::convert::{
    border_ring, color, composition_path, control_bounds, corner_radii, inset as inset_rect, matrix4x4, vec2,
};
use crate::{draw, effects};

/// Composition's `DropShadow.BlurRadius` per unit of Gaussian σ.
/// Calibrated by the conformance suite (`shadow_blur_matches_sigma`).
const BLUR_RADIUS_PER_SIGMA: f64 = 3.0;

/// The content visual of a layer, if any.
enum Content {
    None,
    Sprite(SpriteVisual),
    Shape(ShapeParts),
}

struct ShapeParts {
    visual: ShapeVisual,
    /// Moves the path's bounding box to the visual's origin.
    container: CompositionContainerShape,
    fill: CompositionSpriteShape,
    stroke: CompositionSpriteShape,
    /// The local-space area the shape may cover (`ShapeVisual` clips to
    /// its size).
    extent: Rect,
}

struct ShadowParts {
    sprite: SpriteVisual,
    drop: DropShadow,
    surface: Option<CompositionDrawingSurface>,
    /// What the surface was rasterized from: (outline, scale).
    key: Option<(ShadowShape, Vec2, u64)>,
}

struct Masking {
    layer: LayerVisual,
    /// Holds the mask layer's outer visual, in the masked layer's local space.
    host: ContainerVisual,
    surface: CompositionVisualSurface,
}

struct Drawing {
    key: (ResourceId, Rect, u64),
    surface: CompositionDrawingSurface,
    pixels: (u32, u32),
}

struct Node {
    kind: LayerKind,
    outer: ContainerVisual,
    body: ContainerVisual,
    clipped: ContainerVisual,
    sublayers: ContainerVisual,
    background: Option<SpriteVisual>,
    content: Content,
    border: Option<(ShapeVisual, CompositionSpriteShape)>,
    shadow: Option<ShadowParts>,
    masking: Option<Masking>,
    image: Option<ResourceId>,
    drawing: Option<Drawing>,
    parent: Option<LayerId>,
    children: Vec<LayerId>,
    mask: Option<LayerId>,
    mask_owner: Option<LayerId>,
}

/// Composition surfaces shared by the layers showing the same image.
#[derive(Default)]
struct ImageCache {
    entries: HashMap<ResourceId, (CompositionDrawingSurface, usize)>,
}

/// The Windows.UI.Composition backend. Created by the `attach_*` functions.
pub(crate) struct WinCompBackend {
    ctx: Rc<Context>,
    /// The host's scaled container: the root layer's outer goes in it.
    scaled: ContainerVisual,
    scene: WeakScene,
    nodes: HashMap<LayerId, Node>,
    images: ImageCache,
    scale: f64,
    root: Option<LayerId>,
    reported: bool,
}

fn scale_key(scale: f64) -> u64 {
    scale.to_bits()
}

/// Removes `visual` from whatever container holds it.
fn detach(visual: &impl Interface) {
    let Ok(visual) = visual.cast::<Visual>() else { return };
    if let Ok(parent) = visual.Parent()
        && let Ok(children) = parent.Children()
    {
        let _ = children.Remove(&visual);
    }
}

impl WinCompBackend {
    pub(crate) fn new(ctx: Rc<Context>, scaled: ContainerVisual, scene: WeakScene) -> WinCompBackend {
        WinCompBackend {
            ctx,
            scaled,
            scene,
            nodes: HashMap::new(),
            images: ImageCache::default(),
            scale: 1.0,
            root: None,
            reported: false,
        }
    }

    fn comp(&self) -> &Compositor {
        &self.ctx.compositor
    }

    fn node(&self, id: LayerId) -> &Node {
        self.nodes.get(&id).unwrap_or_else(|| panic!("graphite-windows: unknown layer {id:?}"))
    }

    fn node_mut(&mut self, id: LayerId) -> &mut Node {
        self.nodes.get_mut(&id).unwrap_or_else(|| panic!("graphite-windows: unknown layer {id:?}"))
    }

    /// Composition calls only fail on device or API misuse; report the
    /// first failure and keep going.
    fn report(&mut self, what: &str, result: Result<()>) {
        if let Err(err) = result
            && !std::mem::replace(&mut self.reported, true)
        {
            eprintln!("graphite-windows: {what} failed: {err}");
        }
    }

    fn realize(&self, kind: LayerKind) -> Result<Node> {
        let comp = self.comp();
        let outer = comp.CreateContainerVisual()?;
        let body = comp.CreateContainerVisual()?;
        let clipped = comp.CreateContainerVisual()?;
        let sublayers = comp.CreateContainerVisual()?;
        clipped.Children()?.InsertAtTop(&sublayers)?;
        body.Children()?.InsertAtTop(&clipped)?;
        outer.Children()?.InsertAtTop(&body)?;
        Ok(Node {
            kind,
            outer,
            body,
            clipped,
            sublayers,
            background: None,
            content: Content::None,
            border: None,
            shadow: None,
            masking: None,
            image: None,
            drawing: None,
            parent: None,
            children: Vec::new(),
            mask: None,
            mask_owner: None,
        })
    }

    // ── children ──

    /// Sets the native sublayers of `id` to its children's outers, in order.
    fn sync_children(&self, id: LayerId) -> Result<()> {
        let node = self.node(id);
        let children = node.sublayers.Children()?;
        children.RemoveAll()?;
        for c in &node.children {
            let outer = &self.node(*c).outer;
            detach(outer);
            children.InsertAtTop(outer)?;
        }
        Ok(())
    }

    fn apply_children(&mut self, batch: &CommitBatch) -> Result<()> {
        // Pass 1: detach children that left their parent.
        for (parent, list) in &batch.children {
            let removed: Vec<LayerId> =
                self.node(*parent).children.iter().copied().filter(|c| !list.contains(c)).collect();
            for c in removed {
                let child = self.node_mut(c);
                if child.parent == Some(*parent) {
                    child.parent = None;
                    detach(&child.outer);
                }
            }
        }
        // Pass 2: install the new lists, re-parenting as needed.
        for (parent, list) in &batch.children {
            for &c in list {
                self.clear_mask_owner(c)?;
                let previous = self.node_mut(c).parent.replace(*parent);
                if let Some(prev) = previous.filter(|p| p != parent)
                    && let Some(p) = self.nodes.get_mut(&prev)
                {
                    p.children.retain(|x| *x != c);
                }
            }
            self.node_mut(*parent).children = list.clone();
            self.sync_children(*parent)?;
        }
        Ok(())
    }

    // ── masks ──

    /// Detaches `id` from whatever layer it currently masks.
    fn clear_mask_owner(&mut self, id: LayerId) -> Result<()> {
        if let Some(owner) = self.node_mut(id).mask_owner.take() {
            self.node_mut(owner).mask = None;
            self.unmask(owner)?;
        }
        detach(&self.node(id).outer);
        Ok(())
    }

    /// Removes the mask machinery of `id`, putting its body back in place.
    fn unmask(&mut self, id: LayerId) -> Result<()> {
        let Some(node) = self.nodes.get_mut(&id) else { return Ok(()) };
        let Some(masking) = node.masking.take() else { return Ok(()) };
        masking.host.Children()?.RemoveAll()?;
        masking.layer.Children()?.RemoveAll()?;
        node.outer.Children()?.Remove(&masking.layer)?;
        node.outer.Children()?.InsertAtTop(&node.body)?;
        Ok(())
    }

    fn apply_mask(&mut self, id: LayerId, r: &Resolved) -> Result<()> {
        let old = self.node(id).mask;
        if old == r.mask {
            return Ok(());
        }
        if let Some(old) = old
            && let Some(m) = self.nodes.get_mut(&old)
        {
            m.mask_owner = None;
            detach(&m.outer);
        }
        self.node_mut(id).mask = r.mask;
        let Some(m) = r.mask else {
            return self.unmask(id);
        };
        self.clear_mask_owner(m)?;
        let mask = self.node_mut(m);
        mask.parent = None;
        mask.mask_owner = Some(id);
        let mask_outer = mask.outer.clone();
        if self.node(id).masking.is_none() {
            let masking = self.create_masking()?;
            let node = self.node_mut(id);
            node.outer.Children()?.Remove(&node.body)?;
            masking.layer.Children()?.InsertAtTop(&node.body)?;
            node.outer.Children()?.InsertAtTop(&masking.layer)?;
            node.masking = Some(masking);
        }
        let masking = self.node(id).masking.as_ref().unwrap();
        masking.host.Children()?.RemoveAll()?;
        masking.host.Children()?.InsertAtTop(&mask_outer)?;
        Ok(())
    }

    fn create_masking(&self) -> Result<Masking> {
        let comp = self.comp();
        let host = comp.CreateContainerVisual()?;
        let surface = comp.CreateVisualSurface()?;
        surface.SetSourceVisual(&host)?;
        let mask_brush = comp.CreateSurfaceBrushWithSurface(&surface)?;
        mask_brush.SetStretch(CompositionStretch::Fill)?;
        let factory = comp.CreateEffectFactory(&effects::alpha_mask()?)?;
        let effect = factory.CreateBrush()?;
        effect.SetSourceParameter(&HSTRING::from(effects::MASK), &mask_brush)?;
        let layer = comp.CreateLayerVisual()?;
        layer.SetEffect(&effect)?;
        Ok(Masking { layer, host, surface })
    }

    /// The masked area is the layer's box: content outside it is hidden.
    fn place_masking(&self, id: LayerId, r: &Resolved) -> Result<()> {
        if let Some(m) = &self.node(id).masking {
            m.layer.SetSize(vec2(r.size.width, r.size.height))?;
            // The box's origin in local space (the host's space) is -bounds_offset.
            m.surface.SetSourceOffset(vec2(-r.bounds_offset.x, -r.bounds_offset.y))?;
            m.surface.SetSourceSize(vec2(r.size.width, r.size.height))?;
        }
        Ok(())
    }

    // ── updates ──

    fn apply_update(&mut self, update: &LayerUpdate, is_root: bool) -> Result<()> {
        let id = update.id;
        let changed = update.changed;
        let r = &update.layer;
        let p = &r.props;

        if changed.contains(DirtyBits::GEOMETRY) {
            let node = self.node(id);
            let transform = if is_root { Affine::IDENTITY } else { r.transform };
            node.outer.SetTransformMatrix(matrix4x4(transform))?;
            node.sublayers.SetOffset(Vector3 { X: r.bounds_offset.x as f32, Y: r.bounds_offset.y as f32, Z: 0.0 })?;
        }
        if changed.contains(DirtyBits::APPEARANCE) {
            let node = self.node(id);
            node.outer.SetOpacity(p.opacity)?;
            node.outer.SetIsVisible(!p.hidden)?;
        }
        if changed.intersects(DirtyBits::GEOMETRY | DirtyBits::APPEARANCE) {
            self.apply_decoration(id, r)?;
        }
        if changed.intersects(DirtyBits::GEOMETRY | DirtyBits::SHADOW) {
            self.apply_shadow(id, r)?;
        }
        if changed.contains(DirtyBits::MASK) {
            self.apply_mask(id, r)?;
        }
        if changed.intersects(DirtyBits::GEOMETRY | DirtyBits::MASK) {
            self.place_masking(id, r)?;
        }
        if changed.intersects(DirtyBits::GEOMETRY | DirtyBits::CONTENT) {
            self.apply_content(id, r, changed.contains(DirtyBits::CONTENT))?;
        }
        Ok(())
    }

    /// A clip to `rect` with rounded corners.
    fn rounded_clip(&self, rect: Rect, radius: f64, corners: Corners) -> Result<CompositionClip> {
        let [tl, tr, br, bl] = corner_radii(rect.size, radius, corners);
        let v = |r: f64| vec2(r, r);
        self.comp()
            .CreateRectangleClipWithSidesAndRadius(
                rect.min_x() as f32,
                rect.min_y() as f32,
                rect.max_x() as f32,
                rect.max_y() as f32,
                v(tl),
                v(tr),
                v(br),
                v(bl),
            )?
            .cast()
    }

    /// Background, clip and border: everything that depends on the box.
    fn apply_decoration(&mut self, id: LayerId, r: &Resolved) -> Result<()> {
        let p = &r.props;
        let size = r.size;
        let (radius, corners) = (p.corner_radius, p.masked_corners);
        let border = r.border.filter(|b| b.width > 0.0);
        // Under an opaque border, the background and the clip end at the
        // border's midline, where the border hides their edges.
        let inset = match border {
            Some(b) if b.color.a >= 1.0 => b.width / 2.0,
            _ => 0.0,
        };
        let inner = inset_rect(Rect::from_size(size), inset);
        let inner_radius = if radius > 0.0 { (radius - inset).max(0.0) } else { 0.0 };
        let rounded = corner_radii(inner.size, inner_radius, corners).iter().any(|r| *r > 0.0);

        // Clip of contents and sublayers.
        let clipped = &self.node(id).clipped;
        if p.masks_to_bounds {
            clipped.SetClip(&self.rounded_clip(inner, inner_radius, corners)?)?;
        } else {
            clipped.SetClip(None::<&CompositionClip>)?;
        }

        // Background.
        match p.background {
            Some(bg) => {
                let sprite = match &self.node(id).background {
                    Some(s) => s.clone(),
                    None => {
                        let s = self.comp().CreateSpriteVisual()?;
                        self.node(id).clipped.Children()?.InsertAtBottom(&s)?;
                        self.node_mut(id).background = Some(s.clone());
                        s
                    }
                };
                sprite.SetBrush(&self.comp().CreateColorBrushWithColor(color(bg))?)?;
                sprite.SetOffset(Vector3 { X: inner.min_x() as f32, Y: inner.min_y() as f32, Z: 0.0 })?;
                sprite.SetSize(vec2(inner.width(), inner.height()))?;
                let clip = if rounded {
                    Some(self.rounded_clip(Rect::from_size(inner.size), inner_radius, corners)?)
                } else {
                    None
                };
                sprite.SetClip(clip.as_ref())?;
            }
            None => {
                if let Some(s) = self.node_mut(id).background.take() {
                    detach(&s);
                }
            }
        }

        // Border.
        match border {
            Some(border) => {
                let (visual, shape) = match &self.node(id).border {
                    Some(parts) => parts.clone(),
                    None => {
                        let visual = self.comp().CreateShapeVisual()?;
                        let shape = self.comp().CreateSpriteShape()?;
                        visual.Shapes()?.Append(&shape)?;
                        self.node(id).body.Children()?.InsertAtTop(&visual)?;
                        self.node_mut(id).border = Some((visual.clone(), shape.clone()));
                        (visual, shape)
                    }
                };
                let ring = border_ring(size, radius, corners, border.width);
                let path = composition_path(&self.ctx.d2d_factory, &ring, FillRule::EvenOdd)?;
                shape.SetGeometry(&self.comp().CreatePathGeometryWithPath(&path)?)?;
                shape.SetFillBrush(&self.comp().CreateColorBrushWithColor(color(border.color))?)?;
                visual.SetSize(vec2(size.width, size.height))?;
            }
            None => {
                if let Some((visual, _)) = self.node_mut(id).border.take() {
                    detach(&visual);
                }
            }
        }
        Ok(())
    }

    // ── shadow ──

    fn apply_shadow(&mut self, id: LayerId, r: &Resolved) -> Result<()> {
        let (Some(shadow), Some(shape)) = (&r.props.shadow, &r.shadow_shape) else {
            if let Some(parts) = self.node_mut(id).shadow.take() {
                detach(&parts.sprite);
            }
            return Ok(());
        };
        if self.node(id).shadow.is_none() {
            let sprite = self.comp().CreateSpriteVisual()?;
            let drop = self.comp().CreateDropShadow()?;
            sprite.SetShadow(&drop)?;
            self.node(id).outer.Children()?.InsertAtBottom(&sprite)?;
            self.node_mut(id).shadow = Some(ShadowParts { sprite, drop, surface: None, key: None });
        }
        let opaque = graphite::Color { a: 1.0, ..shadow.color };
        let drop = self.node(id).shadow.as_ref().unwrap().drop.clone();
        drop.SetColor(color(opaque))?;
        drop.SetOpacity(shadow.color.a)?;
        drop.SetOffset(Vector3 { X: shadow.offset.x as f32, Y: shadow.offset.y as f32, Z: 0.0 })?;
        drop.SetBlurRadius((shadow.blur_sigma * BLUR_RADIUS_PER_SIGMA) as f32)?;
        self.rasterize_shadow(id, shape.clone(), r.bounds_offset)
    }

    /// Rasterizes the shadow's outline (box space) into the mask surface.
    fn rasterize_shadow(&mut self, id: LayerId, shape: ShadowShape, bounds_offset: Vec2) -> Result<()> {
        let scale = self.scale;
        let key = (shape.clone(), bounds_offset, scale_key(scale));
        let parts = self.node(id).shadow.as_ref().unwrap();
        if parts.key.as_ref() == Some(&key) {
            return Ok(());
        }
        let outline = shape.path(bounds_offset);
        let sprite = parts.sprite.clone();
        let drop = parts.drop.clone();
        let old_surface = parts.surface.clone();
        // One point of margin keeps the anti-aliased edge inside the surface.
        let Some(bbox) = control_bounds(&outline).map(|b| crate::convert::inset(b, -1.0)) else {
            sprite.SetSize(vec2(0.0, 0.0))?;
            let parts = self.node_mut(id).shadow.as_mut().unwrap();
            parts.key = Some(key);
            return Ok(());
        };
        let (w, h) = ((bbox.width() * scale).ceil().max(1.0) as u32, (bbox.height() * scale).ceil().max(1.0) as u32);
        let surface = match old_surface {
            Some(s) if s.SizeInt32().is_ok_and(|sz| sz.Width == w as i32 && sz.Height == h as i32) => s,
            _ => self.ctx.surface(w, h)?,
        };
        let factory = self.ctx.d2d_factory.clone();
        self.ctx.draw_surface(&surface, |dc, (ox, oy)| {
            let base = Affine::translate(Vec2::new(-bbox.min_x(), -bbox.min_y()))
                .then(Affine::scale(scale, scale))
                .then(Affine::translate(Vec2::new(ox, oy)));
            let list =
                graphite::DisplayList::record(|cx| cx.fill_path(&outline, FillRule::NonZero, graphite::Color::WHITE));
            draw::replay(dc, &factory, &list, base)
        })?;
        let brush = self.comp().CreateSurfaceBrushWithSurface(&surface)?;
        brush.SetStretch(CompositionStretch::Fill)?;
        drop.SetMask(&brush)?;
        sprite.SetOffset(Vector3 { X: bbox.min_x() as f32, Y: bbox.min_y() as f32, Z: 0.0 })?;
        sprite.SetSize(vec2(w as f64 / scale, h as f64 / scale))?;
        let parts = self.node_mut(id).shadow.as_mut().unwrap();
        parts.surface = Some(surface);
        parts.key = Some(key);
        Ok(())
    }

    // ── content ──

    fn set_content(&mut self, id: LayerId, content: Content) -> Result<()> {
        let node = self.node_mut(id);
        match std::mem::replace(&mut node.content, Content::None) {
            Content::Sprite(s) => detach(&s),
            Content::Shape(s) => detach(&s.visual),
            Content::None => {}
        }
        let visual: Option<Visual> = match &content {
            Content::Sprite(s) => Some(s.cast()?),
            Content::Shape(s) => Some(s.visual.cast()?),
            Content::None => None,
        };
        if let Some(visual) = visual {
            node.clipped.Children()?.InsertBelow(&visual, &node.sublayers)?;
        }
        node.content = content;
        Ok(())
    }

    fn content_sprite(&mut self, id: LayerId) -> Result<SpriteVisual> {
        if let Content::Sprite(s) = &self.node(id).content {
            return Ok(s.clone());
        }
        let sprite = self.comp().CreateSpriteVisual()?;
        self.set_content(id, Content::Sprite(sprite.clone()))?;
        Ok(sprite)
    }

    fn apply_content(&mut self, id: LayerId, r: &Resolved, content_changed: bool) -> Result<()> {
        // Image references are counted per node.
        let wanted_image = match &r.content {
            ResolvedContent::Image { image, .. } => Some(image),
            _ => None,
        };
        if self.node(id).image != wanted_image.map(|i| i.id()) {
            if let Some(old) = self.node_mut(id).image.take() {
                self.images.release(old);
            }
            if let Some(image) = wanted_image {
                let surface = self.images.acquire(&self.ctx, image)?;
                self.node_mut(id).image = Some(image.id());
                let sprite = self.content_sprite(id)?;
                let brush = self.comp().CreateSurfaceBrushWithSurface(&surface)?;
                brush.SetStretch(CompositionStretch::Fill)?;
                brush.SetBitmapInterpolationMode(CompositionBitmapInterpolationMode::Linear)?;
                sprite.SetBrush(&brush)?;
            }
        }
        if !matches!(r.content, ResolvedContent::Drawing(_)) {
            self.node_mut(id).drawing = None;
        }

        match &r.content {
            ResolvedContent::None => self.set_content(id, Content::None)?,
            ResolvedContent::Image { rect, .. } => {
                let sprite = self.content_sprite(id)?;
                sprite.SetOffset(Vector3 { X: rect.min_x() as f32, Y: rect.min_y() as f32, Z: 0.0 })?;
                sprite.SetSize(vec2(rect.width(), rect.height()))?;
            }
            ResolvedContent::Drawing(list) => self.apply_drawing(id, list, r)?,
            ResolvedContent::Gradient(gradient) => {
                let sprite = self.content_sprite(id)?;
                if content_changed {
                    sprite.SetBrush(&self.gradient_brush(gradient)?)?;
                }
                sprite.SetSize(vec2(r.size.width, r.size.height))?;
            }
            ResolvedContent::Shape(props) => self.apply_shape(id, props, r, content_changed)?,
        }
        Ok(())
    }

    /// Rasterizes the drawing over the layer's bounds at the backing scale,
    /// like a bitmap backing store (so it is clipped to the bounds).
    fn apply_drawing(&mut self, id: LayerId, list: &graphite::DisplayList, r: &Resolved) -> Result<()> {
        let scale = self.scale;
        let local = Rect { origin: (-r.bounds_offset).to_point(), size: r.size };
        let key = (list.id(), local, scale_key(scale));
        if self.node(id).drawing.as_ref().is_some_and(|d| d.key == key) {
            return Ok(());
        }
        let (w, h) = ((r.size.width * scale).ceil(), (r.size.height * scale).ceil());
        if !(w >= 1.0 && h >= 1.0) || list.is_empty() {
            self.node_mut(id).drawing = None;
            return self.set_content(id, Content::None);
        }
        let (w, h) = (w as u32, h as u32);
        let surface = match self.node_mut(id).drawing.take() {
            Some(d) if d.pixels == (w, h) => d.surface,
            _ => self.ctx.surface(w, h)?,
        };
        let factory = self.ctx.d2d_factory.clone();
        let offset = r.bounds_offset;
        self.ctx.draw_surface(&surface, |dc, (ox, oy)| {
            let base =
                Affine::translate(offset).then(Affine::scale(scale, scale)).then(Affine::translate(Vec2::new(ox, oy)));
            draw::replay(dc, &factory, list, base)
        })?;
        let sprite = self.content_sprite(id)?;
        let brush = self.comp().CreateSurfaceBrushWithSurface(&surface)?;
        brush.SetStretch(CompositionStretch::Fill)?;
        sprite.SetBrush(&brush)?;
        sprite.SetOffset(Vector3 { X: 0.0, Y: 0.0, Z: 0.0 })?;
        sprite.SetSize(vec2(w as f64 / scale, h as f64 / scale))?;
        self.node_mut(id).drawing = Some(Drawing { key, surface, pixels: (w, h) });
        Ok(())
    }

    fn apply_shape(&mut self, id: LayerId, s: &ShapeProps, r: &Resolved, content_changed: bool) -> Result<()> {
        let Some(path) = &s.path else {
            return self.set_content(id, Content::None);
        };
        if content_changed || !matches!(self.node(id).content, Content::Shape(_)) {
            let parts = match &self.node(id).content {
                Content::Shape(parts) => ShapeParts {
                    visual: parts.visual.clone(),
                    container: parts.container.clone(),
                    fill: parts.fill.clone(),
                    stroke: parts.stroke.clone(),
                    extent: parts.extent,
                },
                _ => {
                    let comp = self.comp();
                    let visual = comp.CreateShapeVisual()?;
                    let container = comp.CreateContainerShape()?;
                    let fill = comp.CreateSpriteShape()?;
                    let stroke = comp.CreateSpriteShape()?;
                    container.Shapes()?.Append(&fill)?;
                    container.Shapes()?.Append(&stroke)?;
                    visual.Shapes()?.Append(&container)?;
                    let parts = ShapeParts { visual, container, fill, stroke, extent: Rect::ZERO };
                    let copy = ShapeParts {
                        visual: parts.visual.clone(),
                        container: parts.container.clone(),
                        fill: parts.fill.clone(),
                        stroke: parts.stroke.clone(),
                        extent: Rect::ZERO,
                    };
                    self.set_content(id, Content::Shape(parts))?;
                    copy
                }
            };
            let extent = self.update_shape(&parts, path, s)?;
            if let Content::Shape(p) = &mut self.node_mut(id).content {
                p.extent = extent;
            }
        }
        let Content::Shape(parts) = &self.node(id).content else { return Ok(()) };
        // `ShapeVisual` clips to its size: place it over the path's extent.
        let e = parts.extent;
        parts.visual.SetOffset(Vector3 {
            X: (r.bounds_offset.x + e.min_x()) as f32,
            Y: (r.bounds_offset.y + e.min_y()) as f32,
            Z: 0.0,
        })?;
        parts.visual.SetSize(vec2(e.width(), e.height()))?;
        parts.container.SetOffset(vec2(-e.min_x(), -e.min_y()))?;
        Ok(())
    }

    /// Returns the local-space extent the shape may cover.
    fn update_shape(&self, parts: &ShapeParts, path: &Path, s: &ShapeProps) -> Result<Rect> {
        let comp = self.comp();
        let factory = &self.ctx.d2d_factory;
        // The fill uses the whole path; trimming applies to the stroke only
        // (composition would trim both).
        let fill_path = composition_path(factory, path, s.fill_rule)?;
        parts.fill.SetGeometry(&comp.CreatePathGeometryWithPath(&fill_path)?)?;
        parts.fill.SetFillBrush(
            match &s.fill {
                Some(paint) => Some(self.paint_brush(paint)?),
                None => None,
            }
            .as_ref(),
        )?;

        let mut margin = 1.0;
        match &s.stroke {
            Some(stroke) => {
                let st = &stroke.style;
                let geometry = comp.CreatePathGeometryWithPath(&composition_path(factory, path, FillRule::NonZero)?)?;
                geometry.SetTrimStart(s.stroke_start.clamp(0.0, 1.0) as f32)?;
                geometry.SetTrimEnd(s.stroke_end.clamp(0.0, 1.0) as f32)?;
                let shape = &parts.stroke;
                shape.SetGeometry(&geometry)?;
                shape.SetStrokeBrush(&self.paint_brush(&stroke.paint)?)?;
                shape.SetStrokeThickness(st.width as f32)?;
                let cap = match st.cap {
                    graphite::LineCap::Butt => CompositionStrokeCap::Flat,
                    graphite::LineCap::Round => CompositionStrokeCap::Round,
                    graphite::LineCap::Square => CompositionStrokeCap::Square,
                };
                shape.SetStrokeStartCap(cap)?;
                shape.SetStrokeEndCap(cap)?;
                shape.SetStrokeDashCap(cap)?;
                shape.SetStrokeLineJoin(match st.join {
                    graphite::LineJoin::Miter => CompositionStrokeLineJoin::MiterOrBevel,
                    graphite::LineJoin::Round => CompositionStrokeLineJoin::Round,
                    graphite::LineJoin::Bevel => CompositionStrokeLineJoin::Bevel,
                })?;
                shape.SetStrokeMiterLimit(st.miter_limit as f32)?;
                // Dash lengths are in multiples of the stroke thickness.
                let dashes = shape.StrokeDashArray()?;
                dashes.Clear()?;
                let w = st.width.max(1e-6);
                for d in st.dash.iter() {
                    dashes.Append((d / w) as f32)?;
                }
                shape.SetStrokeDashOffset((st.dash_phase / w) as f32)?;
                margin += st.width / 2.0 * st.miter_limit.max(1.0) + st.width;
            }
            None => {
                parts.stroke.SetGeometry(None::<&windows::UI::Composition::CompositionGeometry>)?;
                parts.stroke.SetStrokeBrush(None::<&CompositionBrush>)?;
            }
        }
        Ok(control_bounds(path).map(|b| crate::convert::inset(b, -margin.ceil())).unwrap_or(Rect::ZERO))
    }

    fn stops(&self, brush: &CompositionGradientBrush, stops: &[GradientStop]) -> Result<()> {
        let list = brush.ColorStops()?;
        for s in stops {
            list.Append(&self.comp().CreateColorGradientStopWithOffsetAndColor(s.offset, color(s.color))?)?;
        }
        brush.SetExtendMode(CompositionGradientExtendMode::Clamp)?;
        // Interpolate in sRGB, like CoreGraphics.
        brush.SetInterpolationSpace(CompositionColorSpace::Rgb)?;
        Ok(())
    }

    /// A gradient in unit coordinates of the visual it paints.
    fn gradient_brush(&self, gradient: &Gradient) -> Result<CompositionBrush> {
        let comp = self.comp();
        match gradient {
            Gradient::Linear { start, end, stops } => {
                let brush = comp.CreateLinearGradientBrush()?;
                self.stops(&brush.cast()?, stops)?;
                brush.SetMappingMode(CompositionMappingMode::Relative)?;
                brush.SetStartPoint(vec2(start.x, start.y))?;
                brush.SetEndPoint(vec2(end.x, end.y))?;
                brush.cast()
            }
            Gradient::Radial { center, radius, stops } => {
                let brush = comp.CreateRadialGradientBrush()?;
                self.stops(&brush.cast()?, stops)?;
                brush.SetMappingMode(CompositionMappingMode::Relative)?;
                brush.SetEllipseCenter(vec2(center.x, center.y))?;
                brush.SetEllipseRadius(vec2(*radius, *radius))?;
                brush.SetGradientOriginOffset(vec2(0.0, 0.0))?;
                brush.cast()
            }
        }
    }

    /// A paint in the shape's (path) coordinates.
    fn paint_brush(&self, paint: &Paint) -> Result<CompositionBrush> {
        let comp = self.comp();
        match paint {
            Paint::Solid(c) => comp.CreateColorBrushWithColor(color(*c))?.cast(),
            Paint::Linear { start, end, stops } => {
                let brush = comp.CreateLinearGradientBrush()?;
                self.stops(&brush.cast()?, stops)?;
                brush.SetMappingMode(CompositionMappingMode::Absolute)?;
                brush.SetStartPoint(vec2(start.x, start.y))?;
                brush.SetEndPoint(vec2(end.x, end.y))?;
                brush.cast()
            }
            Paint::Radial { center, radius, stops } => {
                let brush = comp.CreateRadialGradientBrush()?;
                self.stops(&brush.cast()?, stops)?;
                brush.SetMappingMode(CompositionMappingMode::Absolute)?;
                brush.SetEllipseCenter(vec2(center.x, center.y))?;
                brush.SetEllipseRadius(vec2(*radius, *radius))?;
                brush.SetGradientOriginOffset(vec2(0.0, 0.0))?;
                brush.cast()
            }
        }
    }

    // ── release ──

    fn release(&mut self, id: LayerId) -> Result<()> {
        let Some(node) = self.nodes.remove(&id) else { return Ok(()) };
        detach(&node.outer);
        if let Some(img) = node.image {
            self.images.release(img);
        }
        if let Some(owner) = node.mask_owner
            && self.nodes.get(&owner).is_some_and(|o| o.mask == Some(id))
        {
            self.node_mut(owner).mask = None;
            self.unmask(owner)?;
        }
        if let Some(m) = node.mask.and_then(|m| self.nodes.get_mut(&m)) {
            m.mask_owner = None;
            detach(&m.outer);
        }
        if let Some(p) = node.parent.and_then(|p| self.nodes.get_mut(&p)) {
            p.children.retain(|c| *c != id);
        }
        for c in &node.children {
            if let Some(child) = self.nodes.get_mut(c)
                && child.parent == Some(id)
            {
                child.parent = None;
                detach(&child.outer);
            }
        }
        let _ = node.kind;
        Ok(())
    }

    fn apply_batch(&mut self, batch: &CommitBatch) -> Result<()> {
        let scale_changed = batch.backing_scale != self.scale;
        self.scale = batch.backing_scale;
        if scale_changed {
            let s = self.scale as f32;
            self.scaled.SetScale(Vector3 { X: s, Y: s, Z: 1.0 })?;
        }
        for &(id, kind) in &batch.realized {
            let node = self.realize(kind)?;
            self.nodes.insert(id, node);
        }
        if let Some(root) = batch.root {
            self.root = Some(root);
            self.scaled.Children()?.InsertAtTop(&self.node(root).outer)?;
        }
        let root = self.root;
        for update in &batch.updates {
            let result = self.apply_update(update, Some(update.id) == root);
            self.report("applying a layer update", result);
        }
        let result = self.apply_children(batch);
        self.report("installing sublayers", result);
        for id in &batch.released {
            let result = self.release(*id);
            self.report("releasing a layer", result);
        }
        if scale_changed {
            // Shadow masks are rasterized at the backing scale.
            let stale: Vec<(LayerId, ShadowShape, Vec2)> = self
                .nodes
                .iter()
                .filter_map(|(id, n)| {
                    let (shape, offset, scale) = n.shadow.as_ref()?.key.clone()?;
                    (scale != scale_key(self.scale)).then_some((*id, shape, offset))
                })
                .collect();
            for (id, shape, offset) in stale {
                let result = self.rasterize_shadow(id, shape, offset);
                self.report("rasterizing a shadow", result);
            }
        }
        Ok(())
    }
}

impl ImageCache {
    fn acquire(&mut self, ctx: &Context, image: &graphite::Image) -> Result<CompositionDrawingSurface> {
        if let Some((surface, uses)) = self.entries.get_mut(&image.id()) {
            *uses += 1;
            return Ok(surface.clone());
        }
        let (w, h) = (image.pixel_width().max(1), image.pixel_height().max(1));
        let surface = ctx.surface(w, h)?;
        if image.pixel_width() > 0 && image.pixel_height() > 0 {
            ctx.draw_surface(&surface, |dc, (ox, oy)| {
                let bitmap = draw::create_bitmap(dc, image)?;
                let dst = Rect::new(ox, oy, w as f64, h as f64);
                // SAFETY: a valid bitmap of this context.
                unsafe {
                    dc.SetTransform(&crate::convert::matrix3x2(Affine::IDENTITY));
                    dc.DrawBitmap(
                        &bitmap,
                        Some(&crate::convert::d2d_rect(dst)),
                        1.0,
                        windows::Win32::Graphics::Direct2D::D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                        None,
                        None,
                    );
                }
                Ok(())
            })?;
        }
        self.entries.insert(image.id(), (surface.clone(), 1));
        Ok(surface)
    }

    fn release(&mut self, id: ResourceId) {
        if let Some((_, uses)) = self.entries.get_mut(&id) {
            *uses -= 1;
            if *uses == 0 {
                self.entries.remove(&id);
            }
        }
    }
}

impl Backend for WinCompBackend {
    fn request_commit(&mut self) {
        schedule_commit(self.scene.clone());
    }

    fn apply(&mut self, batch: CommitBatch) {
        let result = self.apply_batch(&batch);
        self.report("applying a commit", result);
    }
}
