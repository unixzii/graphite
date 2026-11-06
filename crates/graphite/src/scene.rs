//! [`Scene`]: owns every layer of one layer tree and drives commits.
//!
//! # Ownership model
//!
//! All [`LayerData`] lives in a slot map inside the scene. Public handles
//! ([`Layer`] and the typed wrappers) are `(Weak<SceneInner>, LayerKey)` and
//! reference count the data they point to. Strong references come from:
//! - every live handle;
//! - a superlayer, for each of its sublayers;
//! - a layer, for its mask;
//! - the scene itself, for the root layer.
//!
//! When the count reaches zero the data is freed. Freeing is deferred through
//! `pending_free` and always happens with no `RefCell` borrow held, because
//! dropping a layer drops its drawer closure, which may own other handles
//! whose `Drop` re-enters the scene.
//!
//! A layer is *attached* when it is reachable from the root through sublayer
//! or mask edges. Only attached layers are committed. The first commit that
//! sees an attached layer *realizes* it (the backend creates a native
//! object); the native object then lives until the layer data is freed, even
//! across detach / re-attach.

use std::cell::{Cell, RefCell};
use std::rc::{Rc, Weak};

use slotmap::{Key, SlotMap, new_key_type};

use crate::backend::Backend;
use crate::canvas::Canvas;
use crate::commit::{CommitBatch, DirtyBits, LayerId, LayerUpdate, ResolvedContentSource, resolve};
use crate::geometry::{Rect, Size};
use crate::layer::{DrawInfo, Layer};
use crate::props::{Contents, Gradient, Gravity, LayerKind, LayerProps, ShapeProps};

new_key_type! {
    pub(crate) struct LayerKey;
}

impl LayerKey {
    pub(crate) fn id(self) -> LayerId {
        LayerId(self.data().as_ffi())
    }
}

pub(crate) type Drawer = Rc<dyn Fn(&mut Canvas, &DrawInfo)>;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Parent {
    Sublayer(LayerKey),
    MaskOf(LayerKey),
}

impl Parent {
    fn key(self) -> LayerKey {
        match self {
            Parent::Sublayer(k) | Parent::MaskOf(k) => k,
        }
    }
}

pub(crate) struct ContentData {
    pub contents: Contents,
    pub gravity: Gravity,
    pub drawer: Option<Drawer>,
    pub needs_display: bool,
}

pub(crate) enum KindData {
    Container,
    Content(ContentData),
    Shape(ShapeProps),
    Gradient(Option<Gradient>),
}

impl KindData {
    pub(crate) fn kind(&self) -> LayerKind {
        match self {
            KindData::Container => LayerKind::Container,
            KindData::Content(_) => LayerKind::Content,
            KindData::Shape(_) => LayerKind::Shape,
            KindData::Gradient(_) => LayerKind::Gradient,
        }
    }

    pub(crate) fn new(kind: LayerKind) -> KindData {
        match kind {
            LayerKind::Container => KindData::Container,
            LayerKind::Content => KindData::Content(ContentData {
                contents: Contents::None,
                gravity: Gravity::default(),
                drawer: None,
                needs_display: false,
            }),
            LayerKind::Shape => KindData::Shape(ShapeProps::default()),
            LayerKind::Gradient => KindData::Gradient(None),
        }
    }
}

pub(crate) struct LayerData {
    pub strong: u32,
    pub parent: Option<Parent>,
    pub children: Vec<LayerKey>,
    pub mask: Option<LayerKey>,
    pub props: LayerProps,
    pub kind: KindData,
    pub dirty: DirtyBits,
    pub in_dirty_list: bool,
    pub attached: bool,
    pub realized: bool,
}

impl LayerData {
    fn new(kind: LayerKind) -> LayerData {
        LayerData {
            strong: 1,
            parent: None,
            children: Vec::new(),
            mask: None,
            props: LayerProps::default(),
            kind: KindData::new(kind),
            dirty: DirtyBits::all(),
            in_dirty_list: false,
            attached: false,
            realized: false,
        }
    }
}

type LayerMap = SlotMap<LayerKey, LayerData>;

pub(crate) struct SceneInner {
    layers: RefCell<LayerMap>,
    root: LayerKey,
    dirty: RefCell<Vec<LayerKey>>,
    pending_free: RefCell<Vec<LayerKey>>,
    draining: Cell<bool>,
    released: RefCell<Vec<LayerId>>,
    backend: RefCell<Option<Box<dyn Backend>>>,
    commit_requested: Cell<bool>,
    committing: Cell<bool>,
    backing_scale: Cell<f64>,
}

/// Sets a flag for the duration of a scope, resetting it even on panic.
struct FlagGuard<'a>(&'a Cell<bool>);

impl Drop for FlagGuard<'_> {
    fn drop(&mut self) {
        self.0.set(false);
    }
}

/// Queues `key` for the next commit if it is attached; detached layers keep
/// their dirty bits and are queued when they get attached.
fn mark(map: &mut LayerMap, dirty: &mut Vec<LayerKey>, key: LayerKey, bits: DirtyBits) {
    if bits.is_empty() {
        return;
    }
    let d = &mut map[key];
    d.dirty |= bits;
    if d.attached && !d.in_dirty_list {
        d.in_dirty_list = true;
        dirty.push(key);
    }
}

/// Updates `attached` for `key`'s whole subtree (sublayers and masks).
fn set_attached(map: &mut LayerMap, dirty: &mut Vec<LayerKey>, key: LayerKey, attached: bool) {
    let mut stack = vec![key];
    while let Some(k) = stack.pop() {
        let d = &mut map[k];
        d.attached = attached;
        if attached {
            if !d.realized {
                d.dirty = DirtyBits::all();
            }
            if !d.dirty.is_empty() && !d.in_dirty_list {
                d.in_dirty_list = true;
                dirty.push(k);
            }
        }
        stack.extend(d.children.iter().copied());
        stack.extend(d.mask);
    }
}

/// Panics if `ancestor` is `key` or one of its ancestors (through sublayer
/// or mask edges).
#[track_caller]
fn assert_not_ancestor(map: &LayerMap, key: LayerKey, ancestor: LayerKey, msg: &str) {
    let mut cur = Some(key);
    while let Some(k) = cur {
        assert!(k != ancestor, "{msg}");
        cur = map[k].parent.map(Parent::key);
    }
}

impl SceneInner {
    // ── handles and reference counting ──

    pub(crate) fn create(this: &Rc<SceneInner>, kind: LayerKind) -> Layer {
        let key = this.layers.borrow_mut().insert(LayerData::new(kind));
        Layer { scene: Rc::downgrade(this), key }
    }

    /// A new strong handle to an existing layer.
    pub(crate) fn handle(this: &Rc<SceneInner>, key: LayerKey) -> Layer {
        this.retain(key);
        Layer { scene: Rc::downgrade(this), key }
    }

    pub(crate) fn contains(&self, key: LayerKey) -> bool {
        self.layers.borrow().contains_key(key)
    }

    pub(crate) fn retain(&self, key: LayerKey) {
        if let Some(d) = self.layers.borrow_mut().get_mut(key) {
            d.strong += 1;
        }
    }

    pub(crate) fn release(&self, key: LayerKey) {
        {
            let mut map = self.layers.borrow_mut();
            let Some(d) = map.get_mut(key) else { return };
            debug_assert!(d.strong > 0);
            d.strong -= 1;
            if d.strong > 0 {
                return;
            }
            self.pending_free.borrow_mut().push(key);
        }
        self.drain();
    }

    fn drain(&self) {
        if self.draining.replace(true) {
            return; // an outer drain loop will pick up whatever we queued
        }
        let _guard = FlagGuard(&self.draining);
        loop {
            let Some(key) = self.pending_free.borrow_mut().pop() else { break };
            let data = {
                let mut map = self.layers.borrow_mut();
                let Some(data) = map.remove(key) else { continue };
                debug_assert!(!data.attached, "attached layers are always referenced by their parent");
                let mut pending = self.pending_free.borrow_mut();
                for &child in data.children.iter().chain(data.mask.iter()) {
                    if let Some(cd) = map.get_mut(child) {
                        cd.parent = None;
                        cd.strong -= 1;
                        if cd.strong == 0 {
                            pending.push(child);
                        }
                    }
                }
                if data.realized {
                    self.released.borrow_mut().push(key.id());
                }
                data
            };
            // May run arbitrary `Drop` code (drawer closures); no borrows held.
            drop(data);
        }
        // A detached layer that was already committed has no dirty parent to
        // trigger the commit that releases its native object.
        self.note_dirty();
    }

    // ── property access ──

    pub(crate) fn read<R>(&self, key: LayerKey, f: impl FnOnce(&LayerData) -> R) -> Option<R> {
        self.layers.borrow().get(key).map(f)
    }

    /// Mutates a layer. `f` returns the dirty bits to record (empty if
    /// nothing changed) and a value that is handed back to the caller, so
    /// that anything with a non-trivial `Drop` (an old drawer) is dropped
    /// after the borrow is released.
    pub(crate) fn update<R>(&self, key: LayerKey, f: impl FnOnce(&mut LayerData) -> (DirtyBits, R)) -> Option<R> {
        let r = {
            let mut map = self.layers.borrow_mut();
            let d = map.get_mut(key)?;
            let (bits, r) = f(d);
            mark(&mut map, &mut self.dirty.borrow_mut(), key, bits);
            r
        };
        self.note_dirty();
        Some(r)
    }

    // ── tree mutation ──

    #[track_caller]
    pub(crate) fn insert_sublayer(&self, parent: LayerKey, child: LayerKey, index: Option<usize>) {
        {
            let mut map = self.layers.borrow_mut();
            let mut dirty = self.dirty.borrow_mut();
            if !map.contains_key(parent) || !map.contains_key(child) {
                return;
            }
            assert!(child != self.root, "the root layer cannot be added as a sublayer");
            assert!(
                !matches!(map[child].parent, Some(Parent::MaskOf(_))),
                "a layer that is used as a mask cannot be added as a sublayer"
            );
            assert_not_ancestor(&map, parent, child, "adding this sublayer would create a cycle");

            match map[child].parent {
                Some(Parent::Sublayer(old)) => {
                    map[old].children.retain(|&k| k != child);
                    mark(&mut map, &mut dirty, old, DirtyBits::CHILDREN);
                }
                Some(Parent::MaskOf(_)) => unreachable!(),
                // The new superlayer takes a strong reference.
                None => map[child].strong += 1,
            }
            let children = &mut map[parent].children;
            let index = index.unwrap_or(children.len()).min(children.len());
            children.insert(index, child);
            mark(&mut map, &mut dirty, parent, DirtyBits::CHILDREN);
            map[child].parent = Some(Parent::Sublayer(parent));

            let attached = map[parent].attached;
            if map[child].attached != attached {
                set_attached(&mut map, &mut dirty, child, attached);
            }
        }
        self.note_dirty();
    }

    pub(crate) fn remove_from_parent(&self, child: LayerKey) {
        {
            let mut map = self.layers.borrow_mut();
            let mut dirty = self.dirty.borrow_mut();
            let Some(parent) = map.get(child).and_then(|d| d.parent) else { return };
            match parent {
                Parent::Sublayer(p) => {
                    map[p].children.retain(|&k| k != child);
                    mark(&mut map, &mut dirty, p, DirtyBits::CHILDREN);
                }
                Parent::MaskOf(owner) => {
                    map[owner].mask = None;
                    mark(&mut map, &mut dirty, owner, DirtyBits::MASK);
                }
            }
            map[child].parent = None;
            if map[child].attached {
                set_attached(&mut map, &mut dirty, child, false);
            }
        }
        self.release(child); // the reference the parent held
        self.note_dirty();
    }

    #[track_caller]
    pub(crate) fn set_mask(&self, owner: LayerKey, mask: Option<LayerKey>) {
        let old = {
            let mut map = self.layers.borrow_mut();
            let mut dirty = self.dirty.borrow_mut();
            if !map.contains_key(owner) || mask.is_some_and(|m| !map.contains_key(m)) {
                return;
            }
            if map[owner].mask == mask {
                return;
            }
            if let Some(m) = mask {
                assert!(m != owner, "a layer cannot be its own mask");
                assert!(m != self.root, "the root layer cannot be a mask");
                assert!(map[m].parent.is_none(), "a mask layer must not have a superlayer or be another layer's mask");
                assert!(map[m].mask.is_none(), "nested masks are not supported: the mask layer has a mask");
                assert!(
                    !matches!(map[owner].parent, Some(Parent::MaskOf(_))),
                    "nested masks are not supported: this layer is itself a mask"
                );
                assert_not_ancestor(&map, owner, m, "a layer cannot be masked by one of its ancestors");
            }
            let old = map[owner].mask.take();
            if let Some(o) = old {
                map[o].parent = None;
                if map[o].attached {
                    set_attached(&mut map, &mut dirty, o, false);
                }
            }
            if let Some(m) = mask {
                map[m].parent = Some(Parent::MaskOf(owner));
                map[m].strong += 1;
                map[owner].mask = Some(m);
                if map[owner].attached {
                    set_attached(&mut map, &mut dirty, m, true);
                }
            }
            mark(&mut map, &mut dirty, owner, DirtyBits::MASK);
            old
        };
        if let Some(o) = old {
            self.release(o);
        }
        self.note_dirty();
    }

    // ── commit ──

    fn note_dirty(&self) {
        if self.commit_requested.get()
            || self.committing.get()
            || (self.dirty.borrow().is_empty() && self.released.borrow().is_empty())
        {
            return;
        }
        if let Ok(mut backend) = self.backend.try_borrow_mut()
            && let Some(backend) = backend.as_mut()
        {
            self.commit_requested.set(true);
            backend.request_commit();
        }
    }

    /// Phase 1: run drawers of attached layers that need display. Runs user
    /// code, so no borrow is held while a drawer executes; drawers may mutate
    /// the scene and those changes land in this commit.
    fn run_drawers(&self) {
        let scale = self.backing_scale.get();
        let jobs: Vec<(LayerKey, Drawer, DrawInfo)> = {
            let mut map = self.layers.borrow_mut();
            let dirty = self.dirty.borrow();
            dirty
                .iter()
                .filter_map(|&k| {
                    let d = map.get_mut(k)?;
                    let bounds = d.props.bounds;
                    match &mut d.kind {
                        KindData::Content(c) if d.attached && c.needs_display => {
                            c.needs_display = false;
                            c.drawer.clone().map(|f| (k, f, DrawInfo { bounds, scale }))
                        }
                        _ => None,
                    }
                })
                .collect()
        };
        for (key, drawer, info) in jobs {
            let mut canvas = Canvas::new();
            drawer(&mut canvas, &info);
            drop(drawer);
            let list = canvas.finish();
            self.update(key, |d| match &mut d.kind {
                KindData::Content(c) => {
                    c.contents = Contents::Drawing(list);
                    (DirtyBits::CONTENT, ())
                }
                _ => (DirtyBits::empty(), ()),
            });
        }
    }

    /// Phase 2: turn the dirty list into a batch.
    fn build_batch(&self) -> CommitBatch {
        let mut batch = CommitBatch { backing_scale: self.backing_scale.get(), ..CommitBatch::default() };
        let keys = std::mem::take(&mut *self.dirty.borrow_mut());
        let mut map = self.layers.borrow_mut();
        // Layers whose drawer asked for display again while drawing.
        let mut redisplay = Vec::new();
        for key in keys {
            let mask_id = map.get(key).and_then(|d| d.mask).map(LayerKey::id);
            let Some(d) = map.get_mut(key) else { continue };
            d.in_dirty_list = false;
            if !d.attached {
                continue; // keeps its dirty bits until it is attached again
            }
            let id = key.id();
            if !d.realized {
                d.realized = true;
                d.dirty = DirtyBits::all();
                batch.realized.push((id, d.kind.kind()));
                if key == self.root {
                    batch.root = Some(id);
                }
            }
            let bits = std::mem::take(&mut d.dirty);
            if bits.contains(DirtyBits::CHILDREN) {
                batch.children.push((id, d.children.iter().map(|k| k.id()).collect()));
            }
            let changed = bits - DirtyBits::CHILDREN;
            if !changed.is_empty() {
                let source = match &d.kind {
                    KindData::Container => ResolvedContentSource::None,
                    KindData::Content(c) => ResolvedContentSource::Contents(&c.contents, c.gravity),
                    KindData::Shape(s) => ResolvedContentSource::Shape(s),
                    KindData::Gradient(g) => ResolvedContentSource::Gradient(g.as_ref()),
                };
                let layer = resolve(&d.props, source, mask_id, key == self.root);
                batch.updates.push(LayerUpdate { id, changed, layer });
            }
            if matches!(&d.kind, KindData::Content(c) if c.needs_display && c.drawer.is_some()) {
                redisplay.push(key);
            }
        }
        let mut dirty = self.dirty.borrow_mut();
        for key in redisplay {
            mark(&mut map, &mut dirty, key, DirtyBits::CONTENT);
        }
        batch.released = std::mem::take(&mut *self.released.borrow_mut());
        batch
    }
}

/// One layer tree, rendered by one backend into one native surface.
///
/// `Scene` is a cheap, reference-counted handle; it is bound to the thread
/// it was created on. Dropping the last `Scene` frees every layer in it and
/// turns all outstanding layer handles inert (setters do nothing, getters
/// return defaults).
#[derive(Clone)]
pub struct Scene {
    pub(crate) inner: Rc<SceneInner>,
}

impl Scene {
    /// Creates a scene whose root layer has the given size. The root's
    /// `position`, `anchor_point` and `transform` are ignored: it always sits
    /// at the top-left of the host surface, and whatever extends past the
    /// surface is clipped.
    pub fn new(root_size: Size) -> Scene {
        let mut layers = LayerMap::with_key();
        let mut root = LayerData::new(LayerKind::Container);
        root.props.bounds = Rect::from_size(root_size);
        root.attached = true;
        root.in_dirty_list = true;
        let root_key = layers.insert(root); // its strong count of 1 is held by the scene
        Scene {
            inner: Rc::new(SceneInner {
                layers: RefCell::new(layers),
                root: root_key,
                dirty: RefCell::new(vec![root_key]),
                pending_free: RefCell::new(Vec::new()),
                draining: Cell::new(false),
                released: RefCell::new(Vec::new()),
                backend: RefCell::new(None),
                commit_requested: Cell::new(false),
                committing: Cell::new(false),
                backing_scale: Cell::new(1.0),
            }),
        }
    }

    pub fn root(&self) -> Layer {
        SceneInner::handle(&self.inner, self.inner.root)
    }

    /// Installs the backend. A scene is rendered by exactly one backend for
    /// its whole life.
    ///
    /// # Panics
    /// If a backend was already set.
    #[track_caller]
    pub fn set_backend(&self, backend: Box<dyn Backend>) {
        {
            let mut slot = self.inner.backend.borrow_mut();
            assert!(slot.is_none(), "a scene's backend can only be set once");
            *slot = Some(backend);
        }
        self.inner.note_dirty();
    }

    /// Whether there are uncommitted changes.
    pub fn needs_commit(&self) -> bool {
        !self.inner.dirty.borrow().is_empty() || !self.inner.released.borrow().is_empty()
    }

    /// Runs drawers that need display, builds a [`CommitBatch`] and hands it
    /// to the backend. Normally called by the backend at its commit point;
    /// call it directly to force a synchronous commit. Does nothing without a
    /// backend or when called re-entrantly.
    pub fn commit(&self) {
        let inner = &*self.inner;
        if inner.committing.get() || inner.backend.borrow().is_none() {
            return;
        }
        inner.committing.set(true);
        {
            let _guard = FlagGuard(&inner.committing);
            inner.run_drawers();
            let batch = inner.build_batch();
            if !batch.is_empty()
                && let Some(backend) = inner.backend.borrow_mut().as_mut()
            {
                backend.apply(batch);
            }
        }
        inner.commit_requested.set(false);
        // Drawers may have asked for another display pass.
        inner.note_dirty();
    }

    /// Device pixels per point of the host surface.
    pub fn backing_scale(&self) -> f64 {
        self.inner.backing_scale.get()
    }

    /// Called by the backend when the surface's scale changes. Every layer
    /// with a drawer is redisplayed at the next commit, and every
    /// non-container layer is committed with [`DirtyBits::CONTENT`] so that
    /// backends re-rasterize scale-dependent contents.
    pub fn set_backing_scale(&self, scale: f64) {
        let inner = &*self.inner;
        if inner.backing_scale.replace(scale) == scale {
            return;
        }
        {
            let mut map = inner.layers.borrow_mut();
            let mut dirty = inner.dirty.borrow_mut();
            let keys: Vec<LayerKey> = map
                .iter_mut()
                .filter_map(|(k, d)| match &mut d.kind {
                    KindData::Container => None,
                    KindData::Content(c) => {
                        c.needs_display |= c.drawer.is_some();
                        Some(k)
                    }
                    KindData::Shape(_) | KindData::Gradient(_) => Some(k),
                })
                .collect();
            for k in keys {
                mark(&mut map, &mut dirty, k, DirtyBits::CONTENT);
            }
        }
        inner.note_dirty();
    }

    /// Number of live layers, including the root and detached layers.
    pub fn layer_count(&self) -> usize {
        self.inner.layers.borrow().len()
    }

    pub fn downgrade(&self) -> WeakScene {
        WeakScene { inner: Rc::downgrade(&self.inner) }
    }

    pub fn ptr_eq(&self, other: &Scene) -> bool {
        Rc::ptr_eq(&self.inner, &other.inner)
    }
}

impl std::fmt::Debug for Scene {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Scene").field("layers", &self.layer_count()).finish()
    }
}

/// A non-owning reference to a [`Scene`], for backends and hosts.
#[derive(Clone)]
pub struct WeakScene {
    inner: Weak<SceneInner>,
}

impl WeakScene {
    pub fn upgrade(&self) -> Option<Scene> {
        self.inner.upgrade().map(|inner| Scene { inner })
    }
}
