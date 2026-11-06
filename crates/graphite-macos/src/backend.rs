//! Maps commit batches onto a Core Animation layer tree.
//!
//! Every graphite layer becomes two `CALayer`s:
//!
//! ```text
//! outer  (CALayer)        bounds = (0,0,w,h), position, anchorPoint, transform,
//!   │                     opacity, hidden, shadow (always with a shadowPath)
//!   └─ body (kind class)  frame = outer.bounds, bounds = graphite bounds,
//!        │                masksToBounds, cornerRadius, background, border,
//!        │                contents / shape / gradient, mask
//!        ├─ image layer   (only for image contents, placed per gravity)
//!        └─ sublayers' outers
//! ```
//!
//! Splitting off `outer` lets the shadow escape the body's `masksToBounds`
//! (graphite's semantics, unlike plain `CALayer`), and keeps `body.bounds`
//! equal to graphite's bounds so that `bounds.origin` scrolling, shape paths
//! and masks are handled natively by Core Animation.

use std::cell::Cell;
use std::collections::HashMap;
use std::rc::Rc;

use block2::RcBlock;
use graphite::{
    Backend, CommitBatch, DirtyBits, Gradient, LayerId, LayerKind, LayerUpdate, Paint, Rect, ResolvedContent,
    ResourceId, ShapeProps, WeakScene,
};
use objc2::rc::Retained;
use objc2_core_foundation::{
    CFRetained, CFRunLoop, CFRunLoopActivity, CFRunLoopObserver, CGPoint, CGRect, CGSize, kCFRunLoopCommonModes,
};
use objc2_core_graphics::CGImage;
use objc2_foundation::NSArray;
use objc2_quartz_core::{
    CAGradientLayer, CALayer, CAShapeLayer, CATransaction, CATransform3D, kCAFillRuleEvenOdd, kCAFillRuleNonZero,
    kCAGradientLayerAxial, kCAGradientLayerRadial, kCAGravityResize, kCALineCapButt, kCALineCapRound, kCALineCapSquare,
    kCALineJoinBevel, kCALineJoinMiter, kCALineJoinRound,
};

use crate::convert::{ca_corners, cf_as_object, cg_affine, cg_color, cg_image, cg_path, cg_point, cg_rect, ns_numbers};
use crate::draw;

/// Run before Core Animation's own commit observer (order 2 000 000), so
/// that our changes reach the screen in the same frame.
const COMMIT_OBSERVER_ORDER: isize = 1_999_000;

enum Body {
    Plain(Retained<CALayer>),
    Shape(Retained<CAShapeLayer>),
    Gradient(Retained<CAGradientLayer>),
}

impl Body {
    fn new(kind: LayerKind) -> Body {
        match kind {
            LayerKind::Container | LayerKind::Content => Body::Plain(CALayer::new()),
            LayerKind::Shape => Body::Shape(CAShapeLayer::new()),
            LayerKind::Gradient => Body::Gradient(CAGradientLayer::new()),
        }
    }

    fn layer(&self) -> &CALayer {
        match self {
            Body::Plain(l) => l,
            Body::Shape(l) => l,
            Body::Gradient(l) => l,
        }
    }
}

struct Node {
    outer: Retained<CALayer>,
    body: Body,
    /// Image contents are drawn by a dedicated first sublayer of `body`, so
    /// that gravity placement does not depend on Core Animation's
    /// orientation-sensitive `contentsGravity`.
    image_layer: Option<Retained<CALayer>>,
    image: Option<ResourceId>,
    /// What `body.contents` was rasterized from: (display list, bounds, scale).
    drawing: Option<(ResourceId, Rect, f64)>,
    parent: Option<LayerId>,
    children: Vec<LayerId>,
    mask: Option<LayerId>,
    mask_owner: Option<LayerId>,
}

impl Node {
    fn new(kind: LayerKind, scale: f64) -> Node {
        let outer = CALayer::new();
        let body = Body::new(kind);
        let b = body.layer();
        // Never rely on platform defaults.
        for l in [&*outer, b] {
            l.setAllowsGroupOpacity(true);
            l.setAllowsEdgeAntialiasing(true);
            l.setContentsScale(scale);
        }
        b.setAnchorPoint(CGPoint::new(0.0, 0.0));
        b.setPosition(CGPoint::new(0.0, 0.0));
        outer.addSublayer(b);
        Node {
            outer,
            body,
            image_layer: None,
            image: None,
            drawing: None,
            parent: None,
            children: Vec::new(),
            mask: None,
            mask_owner: None,
        }
    }
}

/// `CGImage`s shared by the layers showing the same graphite [`graphite::Image`].
#[derive(Default)]
struct ImageCache {
    entries: HashMap<ResourceId, (CFRetained<CGImage>, usize)>,
}

impl ImageCache {
    fn acquire(&mut self, image: &graphite::Image) -> Option<CFRetained<CGImage>> {
        if let Some((cg, uses)) = self.entries.get_mut(&image.id()) {
            *uses += 1;
            return Some(cg.clone());
        }
        let cg = cg_image(image)?;
        self.entries.insert(image.id(), (cg.clone(), 1));
        Some(cg)
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

/// Schedules `Scene::commit` at the end of the current run-loop iteration.
struct Scheduler {
    scene: WeakScene,
    run_loop: Option<CFRetained<CFRunLoop>>,
    pending: Rc<Cell<bool>>,
}

impl Scheduler {
    fn request(&self) {
        let Some(run_loop) = &self.run_loop else { return };
        if self.pending.replace(true) {
            return;
        }
        let scene = self.scene.clone();
        let pending = self.pending.clone();
        let block = RcBlock::new(move |_observer, _activity| {
            pending.set(false);
            if let Some(scene) = scene.upgrade() {
                scene.commit();
            }
        });
        // SAFETY: the default allocator and a valid block are passed.
        let observer = unsafe {
            CFRunLoopObserver::with_handler(
                None,
                CFRunLoopActivity::BeforeWaiting.0,
                false,
                COMMIT_OBSERVER_ORDER,
                Some(&block),
            )
        };
        if let Some(observer) = observer {
            // SAFETY: `kCFRunLoopCommonModes` is a valid constant.
            run_loop.add_observer(Some(&observer), unsafe { kCFRunLoopCommonModes });
            run_loop.wake_up();
        } else {
            self.pending.set(false);
        }
    }
}

/// The Core Animation backend. Created by [`crate::attach_to_layer`] or
/// [`crate::attach_to_view`].
pub(crate) struct CaBackend {
    /// The host layer: the scene's root layer goes in it.
    host_layer: Retained<CALayer>,
    nodes: HashMap<LayerId, Node>,
    images: ImageCache,
    scheduler: Scheduler,
    scale: f64,
    root: Option<LayerId>,
}

impl CaBackend {
    pub(crate) fn new(host_layer: Retained<CALayer>, scene: WeakScene) -> CaBackend {
        CaBackend {
            host_layer,
            nodes: HashMap::new(),
            images: ImageCache::default(),
            scheduler: Scheduler { scene, run_loop: CFRunLoop::current(), pending: Rc::new(Cell::new(false)) },
            scale: 1.0,
            root: None,
        }
    }

    fn node(&self, id: LayerId) -> &Node {
        self.nodes.get(&id).unwrap_or_else(|| panic!("graphite-macos: unknown layer {id:?}"))
    }

    fn node_mut(&mut self, id: LayerId) -> &mut Node {
        self.nodes.get_mut(&id).unwrap_or_else(|| panic!("graphite-macos: unknown layer {id:?}"))
    }

    /// Sets `body.sublayers` to [image layer] + children's outers.
    fn sync_sublayers(&self, id: LayerId) {
        let node = self.node(id);
        let mut layers: Vec<&CALayer> = Vec::with_capacity(node.children.len() + 1);
        if let Some(img) = &node.image_layer {
            layers.push(img);
        }
        layers.extend(node.children.iter().map(|c| &*self.node(*c).outer));
        let array = NSArray::from_slice(&layers);
        // SAFETY: every element is a CALayer.
        unsafe { node.body.layer().setSublayers(Some(&array)) };
    }

    /// Detaches `id` from whatever layer it currently masks.
    fn clear_mask_owner(&mut self, id: LayerId) {
        if let Some(owner) = self.node_mut(id).mask_owner.take() {
            let owner = self.node_mut(owner);
            owner.mask = None;
            // SAFETY: clearing the mask is always valid.
            unsafe { owner.body.layer().setMask(None) };
        }
    }

    fn apply_children(&mut self, batch: &CommitBatch) {
        // Pass 1: detach children that left their parent.
        for (parent, list) in &batch.children {
            let removed: Vec<LayerId> =
                self.node(*parent).children.iter().copied().filter(|c| !list.contains(c)).collect();
            for c in removed {
                let child = self.node_mut(c);
                if child.parent == Some(*parent) {
                    child.parent = None;
                    child.outer.removeFromSuperlayer();
                }
            }
        }
        // Pass 2: install the new lists, re-parenting as needed.
        for (parent, list) in &batch.children {
            for &c in list {
                self.clear_mask_owner(c);
                let previous = self.node_mut(c).parent.replace(*parent);
                if let Some(prev) = previous.filter(|p| p != parent)
                    && let Some(p) = self.nodes.get_mut(&prev)
                {
                    p.children.retain(|x| *x != c);
                }
            }
            self.node_mut(*parent).children = list.clone();
            self.sync_sublayers(*parent);
        }
    }

    fn apply_update(&mut self, update: &LayerUpdate, is_root: bool) {
        let id = update.id;
        let changed = update.changed;
        let r = &update.layer;
        let p = &r.props;
        let scale = self.scale;
        // The local space actually used: bounds with the (snapped) origin.
        let local_bounds = Rect { origin: (-r.bounds_offset).to_point(), size: r.size };

        if changed.contains(DirtyBits::GEOMETRY) {
            let node = self.node(id);
            let outer = &node.outer;
            outer.setBounds(CGRect::new(CGPoint::new(0.0, 0.0), CGSize::new(r.size.width, r.size.height)));
            if is_root {
                outer.setAnchorPoint(CGPoint::new(0.0, 0.0));
                outer.setPosition(CGPoint::new(0.0, 0.0));
                outer.setTransform(CATransform3D::new_affine_transform(cg_affine(graphite::Affine::IDENTITY)));
            } else {
                outer.setAnchorPoint(cg_point(p.anchor_point));
                // `snap_offset` keeps CA's position/anchor model pixel-aligned.
                outer.setPosition(cg_point(p.position + r.snap_offset));
                outer.setTransform(CATransform3D::new_affine_transform(cg_affine(p.transform)));
            }
            node.body.layer().setBounds(cg_rect(local_bounds));
        }

        if changed.contains(DirtyBits::APPEARANCE) {
            let node = self.node(id);
            node.outer.setOpacity(p.opacity);
            node.outer.setHidden(p.hidden);
            let body = node.body.layer();
            body.setMasksToBounds(p.masks_to_bounds);
            body.setBackgroundColor(p.background.map(cg_color).as_deref());
            body.setCornerRadius(p.corner_radius);
            body.setMaskedCorners(ca_corners(p.masked_corners));
            match r.border {
                Some(b) => {
                    body.setBorderWidth(b.width);
                    body.setBorderColor(Some(&cg_color(b.color)));
                }
                None => {
                    body.setBorderWidth(0.0);
                    body.setBorderColor(None);
                }
            }
        }

        if changed.contains(DirtyBits::SHADOW) {
            let outer = &self.node(id).outer;
            match (&p.shadow, &r.shadow_shape) {
                (Some(shadow), Some(shape)) => {
                    let opaque = graphite::Color { a: 1.0, ..shadow.color };
                    outer.setShadowColor(Some(&cg_color(opaque)));
                    outer.setShadowOpacity(shadow.color.a);
                    outer.setShadowOffset(CGSize::new(shadow.offset.x, shadow.offset.y));
                    // Calibrated by the conformance suite (shadow_blur_matches_sigma):
                    // Core Animation's shadowRadius is the Gaussian σ.
                    outer.setShadowRadius(shadow.blur_sigma);
                    // In outer (box) space, grown by the spread.
                    let path = cg_path(&shape.path(r.bounds_offset), graphite::Vec2::ZERO);
                    outer.setShadowPath(Some(&path));
                }
                _ => {
                    outer.setShadowOpacity(0.0);
                    outer.setShadowPath(None);
                }
            }
        }

        if changed.contains(DirtyBits::MASK) {
            let old = self.node(id).mask;
            if old != r.mask {
                if let Some(old) = old {
                    self.node_mut(old).mask_owner = None;
                }
                if let Some(m) = r.mask {
                    self.clear_mask_owner(m);
                    let mask = self.node_mut(m);
                    mask.outer.removeFromSuperlayer();
                    mask.parent = None;
                    mask.mask_owner = Some(id);
                }
                let mask_layer = r.mask.map(|m| self.node(m).outer.clone());
                let node = self.node_mut(id);
                node.mask = r.mask;
                // SAFETY: the mask layer has no superlayer.
                unsafe { node.body.layer().setMask(mask_layer.as_deref()) };
            }
        }

        if changed.contains(DirtyBits::CONTENT) {
            self.apply_content(id, &r.content, scale);
        }
        if changed.intersects(DirtyBits::GEOMETRY | DirtyBits::CONTENT) {
            self.sync_content_geometry(id, &r.content, local_bounds, scale);
        }
    }

    fn apply_content(&mut self, id: LayerId, content: &ResolvedContent, scale: f64) {
        // Image contents live in their own sublayer.
        let wanted_image = match content {
            ResolvedContent::Image { image, .. } => Some(image),
            _ => None,
        };
        let current_image = self.node(id).image;
        if current_image != wanted_image.map(|i| i.id()) {
            if let Some(old) = current_image {
                self.images.release(old);
            }
            let cg = wanted_image.and_then(|i| self.images.acquire(i));
            let node = self.node_mut(id);
            node.image = wanted_image.map(|i| i.id());
            match (cg, &node.image_layer) {
                (Some(cg), Some(layer)) => unsafe { layer.setContents(Some(cf_as_object(&*cg))) },
                (Some(cg), None) => {
                    let layer = CALayer::new();
                    layer.setAllowsEdgeAntialiasing(true);
                    // SAFETY: a static constant.
                    layer.setContentsGravity(unsafe { kCAGravityResize });
                    // SAFETY: a CGImage is valid layer contents.
                    unsafe { layer.setContents(Some(cf_as_object(&*cg))) };
                    node.image_layer = Some(layer);
                    self.sync_sublayers(id);
                }
                (None, Some(_)) => {
                    node.image_layer = None;
                    self.sync_sublayers(id);
                }
                (None, None) => {}
            }
        }
        if let (Some(image), Some(layer)) = (wanted_image, &self.node(id).image_layer) {
            layer.setContentsScale(image.scale());
        }

        let node = self.node(id);
        node.body.layer().setContentsScale(scale);
        match (&node.body, content) {
            (Body::Shape(shape), ResolvedContent::Shape(props)) => apply_shape(shape, props),
            (Body::Gradient(layer), ResolvedContent::Gradient(g)) => apply_gradient(layer, Some(g)),
            (Body::Gradient(layer), _) => apply_gradient(layer, None),
            (Body::Plain(_), ResolvedContent::Drawing(_)) => {} // rasterized in sync_content_geometry
            (Body::Plain(body), _) => {
                // SAFETY: clearing contents is always valid.
                unsafe { body.setContents(None) };
                self.node_mut(id).drawing = None;
            }
            (Body::Shape(_), _) => {}
        }
    }

    /// Placement that depends on both geometry and content.
    fn sync_content_geometry(&mut self, id: LayerId, content: &ResolvedContent, bounds: Rect, scale: f64) {
        match content {
            ResolvedContent::Image { rect, .. } => {
                if let Some(layer) = &self.node(id).image_layer {
                    // `rect` is in box space; the body's coordinates are local space.
                    let local = Rect { origin: rect.origin + bounds.origin.to_vec2(), size: rect.size };
                    layer.setFrame(cg_rect(local));
                }
            }
            ResolvedContent::Drawing(list) => {
                let key = (list.id(), bounds, scale);
                if self.node(id).drawing == Some(key) {
                    return;
                }
                let image = draw::rasterize(list, bounds, scale);
                let node = self.node_mut(id);
                node.drawing = Some(key);
                let body = node.body.layer();
                // SAFETY: a static constant; a CGImage is valid layer contents.
                unsafe {
                    body.setContentsGravity(kCAGravityResize);
                    body.setContents(image.as_deref().map(cf_as_object));
                }
            }
            _ => {}
        }
    }

    fn release(&mut self, id: LayerId) {
        let Some(node) = self.nodes.remove(&id) else { return };
        node.outer.removeFromSuperlayer();
        if let Some(img) = node.image {
            self.images.release(img);
        }
        if let Some(owner) = node.mask_owner
            && let Some(o) = self.nodes.get_mut(&owner)
            && o.mask == Some(id)
        {
            o.mask = None;
            // SAFETY: clearing the mask is always valid.
            unsafe { o.body.layer().setMask(None) };
        }
        if let Some(m) = node.mask.and_then(|m| self.nodes.get_mut(&m)) {
            m.mask_owner = None;
        }
        if let Some(p) = node.parent.and_then(|p| self.nodes.get_mut(&p)) {
            p.children.retain(|c| *c != id);
        }
        for c in &node.children {
            if let Some(child) = self.nodes.get_mut(c)
                && child.parent == Some(id)
            {
                child.parent = None;
                child.outer.removeFromSuperlayer();
            }
        }
    }
}

fn paint_color(paint: &Paint) -> graphite::Color {
    match paint {
        Paint::Solid(c) => *c,
        // TODO: gradient fills/strokes on shape layers need a gradient layer
        // masked by the shape; degrade to the first stop for now.
        Paint::Linear { stops, .. } | Paint::Radial { stops, .. } => {
            stops.first().map(|s| s.color).unwrap_or(graphite::Color::TRANSPARENT)
        }
    }
}

fn apply_shape(layer: &CAShapeLayer, s: &ShapeProps) {
    layer.setPath(s.path.as_ref().map(|p| cg_path(p, graphite::Vec2::ZERO)).as_deref());
    layer.setFillColor(s.fill.as_ref().map(|f| cg_color(paint_color(f))).as_deref());
    // SAFETY: static constants.
    unsafe {
        layer.setFillRule(match s.fill_rule {
            graphite::FillRule::NonZero => kCAFillRuleNonZero,
            graphite::FillRule::EvenOdd => kCAFillRuleEvenOdd,
        });
    }
    match &s.stroke {
        Some(stroke) => {
            let st = &stroke.style;
            layer.setStrokeColor(Some(&cg_color(paint_color(&stroke.paint))));
            layer.setLineWidth(st.width);
            layer.setMiterLimit(st.miter_limit);
            // SAFETY: static constants.
            unsafe {
                layer.setLineCap(match st.cap {
                    graphite::LineCap::Butt => kCALineCapButt,
                    graphite::LineCap::Round => kCALineCapRound,
                    graphite::LineCap::Square => kCALineCapSquare,
                });
                layer.setLineJoin(match st.join {
                    graphite::LineJoin::Miter => kCALineJoinMiter,
                    graphite::LineJoin::Round => kCALineJoinRound,
                    graphite::LineJoin::Bevel => kCALineJoinBevel,
                });
            }
            if st.dash.is_empty() {
                layer.setLineDashPattern(None);
            } else {
                layer.setLineDashPattern(Some(&ns_numbers(st.dash.iter().copied())));
            }
            layer.setLineDashPhase(st.dash_phase);
        }
        None => {
            layer.setStrokeColor(None);
            layer.setLineWidth(0.0);
        }
    }
    layer.setStrokeStart(s.stroke_start);
    layer.setStrokeEnd(s.stroke_end);
}

fn apply_gradient(layer: &CAGradientLayer, gradient: Option<&Gradient>) {
    let Some(gradient) = gradient else {
        // SAFETY: clearing the colors is always valid.
        unsafe { layer.setColors(None) };
        return;
    };
    let (stops, kind, start, end) = match gradient {
        Gradient::Linear { start, end, stops } => {
            // SAFETY: a static constant.
            (stops, unsafe { kCAGradientLayerAxial }, *start, *end)
        }
        Gradient::Radial { center, radius, stops } => {
            // CA's radial gradient is an ellipse centred at startPoint whose
            // half-axes are (endPoint - startPoint), in unit space.
            let end = graphite::Point::new(center.x + radius, center.y + radius);
            // SAFETY: a static constant.
            (stops, unsafe { kCAGradientLayerRadial }, *center, end)
        }
    };
    let colors: Vec<CFRetained<objc2_core_graphics::CGColor>> = stops.iter().map(|s| cg_color(s.color)).collect();
    let objects: Vec<&objc2::runtime::AnyObject> = colors.iter().map(|c| cf_as_object(&**c)).collect();
    let array = NSArray::from_slice(&objects);
    layer.setType(kind);
    // SAFETY: an array of CGColors is what `colors` expects.
    unsafe { layer.setColors(Some(&array)) };
    layer.setLocations(Some(&ns_numbers(stops.iter().map(|s| s.offset as f64))));
    layer.setStartPoint(cg_point(start));
    layer.setEndPoint(cg_point(end));
}

impl Backend for CaBackend {
    fn request_commit(&mut self) {
        self.scheduler.request();
    }

    fn apply(&mut self, batch: CommitBatch) {
        CATransaction::begin();
        CATransaction::setDisableActions(true);

        let scale_changed = batch.backing_scale != self.scale;
        self.scale = batch.backing_scale;
        if scale_changed {
            for node in self.nodes.values() {
                node.outer.setContentsScale(self.scale);
                node.body.layer().setContentsScale(self.scale);
            }
        }

        for &(id, kind) in &batch.realized {
            self.nodes.insert(id, Node::new(kind, self.scale));
        }
        if let Some(root) = batch.root {
            self.root = Some(root);
            self.host_layer.addSublayer(&self.node(root).outer);
        }
        let root_id = self.root;

        // Updates before children: realized layers need geometry before they
        // are linked; masks are re-homed defensively in both passes.
        for update in &batch.updates {
            self.apply_update(update, Some(update.id) == root_id);
        }
        self.apply_children(&batch);
        for id in &batch.released {
            self.release(*id);
        }

        CATransaction::commit();
    }
}
