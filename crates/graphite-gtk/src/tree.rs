//! The GSK side of a scene: one cached render node per layer.
//!
//! GSK has no retained layer tree; a widget emits a fresh node tree on every
//! `snapshot()`. This module keeps one immutable `GskRenderNode` per layer
//! and rebuilds only the layers a commit touched, plus their ancestors.
//! Reusing unchanged nodes (same pointers) lets GSK diff damage and keep its
//! texture, glyph and path caches warm.
//!
//! Each layer is composed like this (box space unless noted):
//!
//! ```text
//! transform(resolved.transform)        box space → superlayer's local space
//!   opacity(props.opacity)             group opacity
//!     shadow                           blurred fill of the shadow shape, not clipped
//!     mask(mask layer, local space)    alpha mask
//!       clip(rounded bounds)           only with masks_to_bounds
//!         background                   rounded by corner_radius
//!         contents                     image | drawing (clipped to bounds) | shape | gradient
//!         translate(bounds_offset)     sublayers, in local space
//!         border                       inside the bounds, above sublayers
//! ```

use std::collections::HashMap;

use graphite::{
    CommitBatch, Corners, DirtyBits, Gradient, LayerId, LayerUpdate, Rect, Resolved, ResolvedContent, ResourceId,
    ShadowShape, ShapeProps, Size, Vec2,
};
use gtk::prelude::*;
use gtk::{gdk, gsk};

use crate::{convert, draw};

/// GSK's blur node takes a radius of twice the Gaussian σ. Calibrated by
/// the conformance suite (`shadow_blur_matches_sigma`): for σ = 4 the
/// Vulkan and GL renderers measure 3.8 at the 1σ point and 3.5 at the 2σ
/// point (GSK's kernel has slightly short tails).
const BLUR_RADIUS_PER_SIGMA: f64 = 2.0;

/// The outset shadow node's blur radius, like CSS box-shadow's, is 2σ.
/// Calibrated by `shadow_blur_matches_sigma` (opaque background).
const OUTSET_BLUR_PER_SIGMA: f64 = 2.0;

struct Node {
    /// The latest resolved properties; `None` until the first update.
    layer: Option<Resolved>,
    parent: Option<LayerId>,
    children: Vec<LayerId>,
    mask: Option<LayerId>,
    mask_owner: Option<LayerId>,
    image: Option<(ResourceId, gdk::Texture)>,
    /// The drawing's node, in local space, keyed by display list.
    drawing: Option<(ResourceId, Option<gsk::RenderNode>)>,
    /// The shape's node, in local space.
    shape: Option<gsk::RenderNode>,
    /// `None`: stale. `Some(None)`: draws nothing.
    cached: Option<Option<gsk::RenderNode>>,
}

impl Node {
    fn new() -> Node {
        Node {
            layer: None,
            parent: None,
            children: Vec::new(),
            mask: None,
            mask_owner: None,
            image: None,
            drawing: None,
            shape: None,
            cached: None,
        }
    }
}

/// Textures shared by the layers showing the same `graphite::Image`.
#[derive(Default)]
struct TextureCache {
    entries: HashMap<ResourceId, (gdk::Texture, usize)>,
}

impl TextureCache {
    fn acquire(&mut self, image: &graphite::Image) -> gdk::Texture {
        let entry = self.entries.entry(image.id()).or_insert_with(|| (convert::texture(image), 0));
        entry.1 += 1;
        entry.0.clone()
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

/// What an applied batch requires from the widget.
pub(crate) struct Applied {
    pub redraw: bool,
    pub resize: bool,
}

#[derive(Default)]
pub(crate) struct Tree {
    nodes: HashMap<LayerId, Node>,
    root: Option<LayerId>,
    textures: TextureCache,
}

impl Tree {
    fn node_mut(&mut self, id: LayerId) -> &mut Node {
        self.nodes.get_mut(&id).unwrap_or_else(|| panic!("graphite-gtk: unknown layer {id:?}"))
    }

    /// Marks `id` and everything above it (through sublayer and mask edges)
    /// for rebuilding.
    fn invalidate(&mut self, id: LayerId) {
        let mut cur = Some(id);
        while let Some(c) = cur {
            let Some(node) = self.nodes.get_mut(&c) else { break };
            node.cached = None;
            cur = node.parent.or(node.mask_owner);
        }
    }

    /// The root layer's size, once known.
    pub(crate) fn root_size(&self) -> Option<Size> {
        let root = self.nodes.get(&self.root?)?;
        root.layer.as_ref().map(|l| l.props.bounds.size)
    }

    pub(crate) fn apply(&mut self, batch: &CommitBatch) -> Applied {
        let root_size = self.root_size();
        for &(id, _) in &batch.realized {
            self.nodes.insert(id, Node::new());
        }
        if let Some(root) = batch.root {
            self.root = Some(root);
        }
        for update in &batch.updates {
            self.apply_update(update);
        }
        self.apply_children(batch);
        for id in &batch.released {
            self.release(*id);
        }
        let changed = !batch.realized.is_empty()
            || !batch.updates.is_empty()
            || !batch.children.is_empty()
            || !batch.released.is_empty();
        Applied { redraw: changed, resize: self.root_size() != root_size }
    }

    fn apply_update(&mut self, update: &LayerUpdate) {
        let id = update.id;
        let r = &update.layer;
        if update.changed.contains(DirtyBits::MASK) {
            let old = self.node_mut(id).mask;
            if old != r.mask {
                if let Some(old) = old.and_then(|o| self.nodes.get_mut(&o)) {
                    old.mask_owner = None;
                }
                if let Some(m) = r.mask {
                    let mask = self.node_mut(m);
                    mask.mask_owner = Some(id);
                }
                self.node_mut(id).mask = r.mask;
            }
        }
        if update.changed.contains(DirtyBits::CONTENT) {
            self.apply_content(id, &r.content);
        }
        self.node_mut(id).layer = Some(r.clone());
        self.invalidate(id);
    }

    fn apply_content(&mut self, id: LayerId, content: &ResolvedContent) {
        // Image textures (shared between layers).
        let wanted = match content {
            ResolvedContent::Image { image, .. } => Some(image),
            _ => None,
        };
        let current = self.node_mut(id).image.as_ref().map(|(rid, _)| *rid);
        if current != wanted.map(|i| i.id()) {
            if let Some(old) = current {
                self.textures.release(old);
            }
            let texture = wanted.map(|i| (i.id(), self.textures.acquire(i)));
            self.node_mut(id).image = texture;
        }

        let node = self.node_mut(id);
        match content {
            ResolvedContent::Drawing(list) => {
                if node.drawing.as_ref().map(|(rid, _)| *rid) != Some(list.id()) {
                    node.drawing = Some((list.id(), draw::display_list_node(list)));
                }
            }
            _ => node.drawing = None,
        }
        node.shape = match content {
            ResolvedContent::Shape(shape) => shape_node(shape),
            _ => None,
        };
    }

    fn apply_children(&mut self, batch: &CommitBatch) {
        // Pass 1: children that left a listed parent.
        for (parent, list) in &batch.children {
            let removed: Vec<LayerId> =
                self.node_mut(*parent).children.iter().copied().filter(|c| !list.contains(c)).collect();
            for c in removed {
                let child = self.node_mut(c);
                if child.parent == Some(*parent) {
                    child.parent = None;
                }
            }
        }
        // Pass 2: install the new lists.
        for (parent, list) in &batch.children {
            for &c in list {
                if let Some(owner) = self.node_mut(c).mask_owner.take()
                    && let Some(o) = self.nodes.get_mut(&owner)
                    && o.mask == Some(c)
                {
                    o.mask = None;
                }
                let previous = self.node_mut(c).parent.replace(*parent);
                if let Some(prev) = previous.filter(|p| p != parent)
                    && let Some(p) = self.nodes.get_mut(&prev)
                {
                    p.children.retain(|x| *x != c);
                    p.cached = None;
                }
            }
            self.node_mut(*parent).children = list.clone();
            self.invalidate(*parent);
        }
    }

    fn release(&mut self, id: LayerId) {
        let Some(node) = self.nodes.remove(&id) else { return };
        if let Some((rid, _)) = node.image {
            self.textures.release(rid);
        }
        for c in &node.children {
            if let Some(child) = self.nodes.get_mut(c)
                && child.parent == Some(id)
            {
                child.parent = None;
            }
        }
        if let Some(m) = node.mask.and_then(|m| self.nodes.get_mut(&m)) {
            m.mask_owner = None;
        }
        if let Some(owner) = node.mask_owner {
            if let Some(o) = self.nodes.get_mut(&owner)
                && o.mask == Some(id)
            {
                o.mask = None;
            }
            self.invalidate(owner);
        }
        if let Some(p) = node.parent {
            if let Some(parent) = self.nodes.get_mut(&p) {
                parent.children.retain(|c| *c != id);
            }
            self.invalidate(p);
        }
    }

    /// The node for the whole scene, rebuilding stale layers.
    pub(crate) fn root_node(&mut self) -> Option<gsk::RenderNode> {
        let root = self.root?;
        self.build(root)
    }

    fn build(&mut self, id: LayerId) -> Option<gsk::RenderNode> {
        let node = self.nodes.get(&id)?;
        if let Some(cached) = &node.cached {
            return cached.clone();
        }
        let children = node.children.clone();
        let mask = node.mask;
        let child_nodes: Vec<gsk::RenderNode> = children.iter().filter_map(|c| self.build(*c)).collect();
        let mask_node = mask.and_then(|m| self.build(m));
        let node = &self.nodes[&id];
        let built = match &node.layer {
            Some(layer) if !layer.props.hidden => compose(node, layer, child_nodes, mask.map(|_| mask_node)),
            _ => None,
        };
        self.node_mut(id).cached = Some(built.clone());
        built
    }
}

fn shape_node(shape: &ShapeProps) -> Option<gsk::RenderNode> {
    let path = convert::path(shape.path.as_ref()?, Vec2::ZERO);
    let s = gtk::Snapshot::new();
    if let Some(fill) = &shape.fill {
        paint_path(&s, &path, Some(convert::fill_rule(shape.fill_rule)), None, fill);
    }
    if let Some(stroke) = &shape.stroke
        && let Some(trimmed) = convert::trim(&path, shape.stroke_start, shape.stroke_end)
    {
        let style = convert::stroke(&stroke.style);
        paint_path(&s, &trimmed, None, Some(&style), &stroke.paint);
    }
    s.to_node()
}

/// Fills (`rule`) or strokes (`stroke`) `path` with `paint`.
fn paint_path(
    s: &gtk::Snapshot,
    path: &gsk::Path,
    rule: Option<gsk::FillRule>,
    stroke: Option<&gsk::Stroke>,
    paint: &graphite::Paint,
) {
    match (paint, rule, stroke) {
        (graphite::Paint::Solid(c), Some(rule), _) => s.append_fill(path, rule, &convert::rgba(*c)),
        (graphite::Paint::Solid(c), None, Some(st)) => s.append_stroke(path, st, &convert::rgba(*c)),
        (gradient, rule, stroke) => {
            let bounds = match stroke {
                Some(st) => path.stroke_bounds(st),
                None => path.bounds(),
            };
            let Some(bounds) = bounds else { return };
            match (rule, stroke) {
                (Some(rule), _) => s.push_fill(path, rule),
                (None, Some(st)) => s.push_stroke(path, st),
                (None, None) => return,
            }
            draw::append_paint(s, gradient, &bounds);
            s.pop();
        }
    }
}

/// `mask`: `None` without a mask; `Some(None)` for a mask that draws nothing.
fn compose(
    node: &Node,
    layer: &Resolved,
    children: Vec<gsk::RenderNode>,
    mask: Option<Option<gsk::RenderNode>>,
) -> Option<gsk::RenderNode> {
    let p = &layer.props;
    let size = layer.size;
    let bounds = Rect::from_size(size);
    let to_local = convert::offset(layer.bounds_offset);
    let rounded = p.corner_radius > 0.0 && !p.masked_corners.is_empty();
    let s = gtk::Snapshot::new();

    s.save();
    s.transform(convert::transform(layer.transform).as_ref());
    let faded = p.opacity < 1.0;
    if faded {
        s.push_opacity(p.opacity as f64);
    }

    if let (Some(shadow), Some(shape)) = (&p.shadow, &layer.shadow_shape) {
        // Under an opaque, unmasked background the shadow's interior is
        // never visible, so GSK's analytic outset shadow (what GTK uses for
        // CSS box-shadow) is exact there, and cheaper than blurring. It also
        // avoids drivers that render blur nodes wrongly (virgl).
        let hides_interior = p.background.is_some_and(|c| c.a >= 1.0) && mask.is_none();
        match shape {
            ShadowShape::RoundedRect { rect, radius, corners, spread } if hides_interior => s.append_outset_shadow(
                // GSK grows the outline by the spread the same way Core does
                // (rounded corners grow, square ones stay square).
                &convert::rounded_rect(*rect, *radius, *corners),
                &convert::rgba(shadow.color),
                shadow.offset.x as f32,
                shadow.offset.y as f32,
                *spread as f32,
                (shadow.blur_sigma * OUTSET_BLUR_PER_SIGMA) as f32,
            ),
            _ => {
                let path = convert::path(&shape.path(layer.bounds_offset), Vec2::ZERO);
                s.save();
                s.translate(&convert::offset(shadow.offset));
                let blurred = shadow.blur_sigma > 0.0;
                if blurred {
                    s.push_blur(shadow.blur_sigma * BLUR_RADIUS_PER_SIGMA);
                }
                s.append_fill(&path, gsk::FillRule::Winding, &convert::rgba(shadow.color));
                if blurred {
                    s.pop();
                }
                s.restore();
            }
        }
    }

    if let Some(mask) = &mask {
        s.push_mask(gsk::MaskMode::Alpha);
        if let Some(mask) = mask {
            s.save();
            s.translate(&to_local);
            s.append_node(mask);
            s.restore();
        }
        s.pop();
    }

    if p.masks_to_bounds {
        if rounded {
            s.push_rounded_clip(&convert::rounded_rect(bounds, p.corner_radius, p.masked_corners));
        } else {
            s.push_clip(&convert::rect(bounds));
        }
    }

    if let Some(background) = p.background {
        if rounded {
            s.push_rounded_clip(&convert::rounded_rect(bounds, p.corner_radius, p.masked_corners));
            s.append_color(&convert::rgba(background), &convert::rect(bounds));
            s.pop();
        } else {
            s.append_color(&convert::rgba(background), &convert::rect(bounds));
        }
    }

    match &layer.content {
        ResolvedContent::Image { rect, .. } => {
            if let Some((_, texture)) = &node.image {
                s.append_scaled_texture(texture, gsk::ScalingFilter::Linear, &convert::rect(*rect));
            }
        }
        ResolvedContent::Drawing(_) => {
            if let Some((_, Some(drawing))) = &node.drawing {
                // Like a backing store, contents do not extend past the bounds.
                s.push_clip(&draw::clip_to(bounds));
                s.save();
                s.translate(&to_local);
                s.append_node(drawing);
                s.restore();
                s.pop();
            }
        }
        ResolvedContent::Shape(_) => {
            if let Some(shape) = &node.shape {
                s.save();
                s.translate(&to_local);
                s.append_node(shape);
                s.restore();
            }
        }
        ResolvedContent::Gradient(gradient) => append_gradient(&s, gradient, size),
        ResolvedContent::None => {}
    }

    if !children.is_empty() {
        s.save();
        s.translate(&to_local);
        for child in &children {
            s.append_node(child);
        }
        s.restore();
    }

    if let Some(border) = layer.border.filter(|b| b.width > 0.0) {
        let corners = if rounded { p.masked_corners } else { Corners::empty() };
        let outline = convert::rounded_rect(bounds, p.corner_radius, corners);
        let w = border.width as f32;
        let c = convert::rgba(border.color);
        s.append_border(&outline, &[w; 4], &[c; 4]);
    }

    if p.masks_to_bounds {
        s.pop();
    }
    if mask.is_some() {
        s.pop();
    }
    if faded {
        s.pop();
    }
    s.restore();
    s.to_node()
}

/// A gradient in unit coordinates of `size`, filling the box.
fn append_gradient(s: &gtk::Snapshot, gradient: &Gradient, size: Size) {
    let bounds = convert::rect(Rect::from_size(size));
    let at = |p: graphite::Point| gtk::graphene::Point::new((p.x * size.width) as f32, (p.y * size.height) as f32);
    match gradient {
        Gradient::Linear { start, end, stops } => {
            s.append_linear_gradient(&bounds, &at(*start), &at(*end), &convert::color_stops(stops))
        }
        Gradient::Radial { center, radius, stops } => s.append_radial_gradient(
            &bounds,
            &at(*center),
            (radius * size.width) as f32,
            (radius * size.height) as f32,
            0.0,
            1.0,
            &convert::color_stops(stops),
        ),
    }
}
