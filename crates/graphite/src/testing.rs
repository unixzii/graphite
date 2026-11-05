//! A recording backend for tests.
//!
//! [`Recorder`] keeps every [`CommitBatch`] it receives and replays them into
//! a [`MirrorTree`], a stand-in for a native compositor tree. Applying a batch
//! asserts the [`CommitBatch`] contract (ids realized before use, never
//! reused, children lists only naming live layers, …), so any test that
//! commits also checks that Core emits well-formed batches.

use std::cell::{Ref, RefCell};
use std::collections::HashMap;
use std::fmt::Write as _;
use std::rc::Rc;

use crate::backend::Backend;
use crate::commit::{CommitBatch, DirtyBits, LayerId, Resolved};
use crate::props::LayerKind;

/// Records what a scene commits. Clone it to keep a handle while the scene
/// owns the backend returned by [`Recorder::backend`].
#[derive(Clone, Default)]
pub struct Recorder {
    state: Rc<RefCell<RecorderState>>,
}

#[derive(Default)]
struct RecorderState {
    commit_requests: usize,
    batches: Vec<CommitBatch>,
    tree: MirrorTree,
}

impl Recorder {
    pub fn new() -> Recorder {
        Recorder::default()
    }

    /// A backend feeding this recorder; pass it to [`crate::Scene::set_backend`].
    pub fn backend(&self) -> Box<dyn Backend> {
        Box::new(RecordingBackend { state: self.state.clone() })
    }

    /// How many times the scene asked for a commit.
    pub fn commit_requests(&self) -> usize {
        self.state.borrow().commit_requests
    }

    pub fn batches(&self) -> Ref<'_, [CommitBatch]> {
        Ref::map(self.state.borrow(), |s| s.batches.as_slice())
    }

    /// The most recent batch.
    ///
    /// # Panics
    /// If nothing was committed yet.
    pub fn last(&self) -> Ref<'_, CommitBatch> {
        Ref::map(self.state.borrow(), |s| s.batches.last().expect("no batch was committed"))
    }

    /// Removes and returns all recorded batches (the mirror tree is kept).
    pub fn take_batches(&self) -> Vec<CommitBatch> {
        std::mem::take(&mut self.state.borrow_mut().batches)
    }

    pub fn tree(&self) -> Ref<'_, MirrorTree> {
        Ref::map(self.state.borrow(), |s| &s.tree)
    }
}

struct RecordingBackend {
    state: Rc<RefCell<RecorderState>>,
}

impl Backend for RecordingBackend {
    fn request_commit(&mut self) {
        self.state.borrow_mut().commit_requests += 1;
    }

    fn apply(&mut self, batch: CommitBatch) {
        let mut state = self.state.borrow_mut();
        state.tree.apply(&batch);
        state.batches.push(batch);
    }
}

/// A node of the [`MirrorTree`]: what a native compositor would hold.
#[derive(Clone, Debug)]
pub struct MirrorNode {
    pub kind: LayerKind,
    /// The native parent. A detached-but-alive layer has none.
    pub parent: Option<LayerId>,
    pub children: Vec<LayerId>,
    /// The latest properties received.
    pub layer: Resolved,
}

/// The native-side view of a scene, rebuilt purely from commit batches.
#[derive(Default, Debug)]
pub struct MirrorTree {
    root: Option<LayerId>,
    nodes: HashMap<LayerId, MirrorNode>,
    pending: HashMap<LayerId, LayerKind>,
}

impl MirrorTree {
    pub fn root(&self) -> Option<LayerId> {
        self.root
    }

    pub fn len(&self) -> usize {
        self.nodes.len()
    }

    pub fn is_empty(&self) -> bool {
        self.nodes.is_empty()
    }

    pub fn contains(&self, id: LayerId) -> bool {
        self.nodes.contains_key(&id)
    }

    pub fn node(&self, id: LayerId) -> Option<&MirrorNode> {
        self.nodes.get(&id)
    }

    pub fn children(&self, id: LayerId) -> &[LayerId] {
        self.nodes.get(&id).map_or(&[], |n| n.children.as_slice())
    }

    /// The subtree under the root as indented text, e.g. for snapshot asserts.
    pub fn dump(&self) -> String {
        fn walk(tree: &MirrorTree, id: LayerId, depth: usize, out: &mut String) {
            let n = &tree.nodes[&id];
            let _ = writeln!(out, "{:indent$}{:?}#{}", "", n.kind, id.get(), indent = depth * 2);
            for &c in &n.children {
                walk(tree, c, depth + 1, out);
            }
        }
        let mut out = String::new();
        if let Some(root) = self.root {
            walk(self, root, 0, &mut out);
        }
        out
    }

    /// Applies a batch, asserting the [`CommitBatch`] contract.
    #[track_caller]
    pub fn apply(&mut self, batch: &CommitBatch) {
        // 1. Realize. Properties arrive with the updates below.
        for &(id, kind) in &batch.realized {
            assert!(!self.nodes.contains_key(&id) && !self.pending.contains_key(&id), "{id:?} realized twice");
            self.pending.insert(id, kind);
        }
        if let Some(root) = batch.root {
            assert!(self.root.is_none(), "root announced twice");
            assert!(self.pending.contains_key(&root), "root not realized in the batch that announces it");
            self.root = Some(root);
        }

        // 3 before 2: realized nodes get their first properties before they
        // are linked into the tree.
        for update in &batch.updates {
            assert!(!update.changed.contains(DirtyBits::CHILDREN), "CHILDREN in a property update");
            if let Some(kind) = self.pending.remove(&update.id) {
                assert_eq!(update.changed, DirtyBits::all() - DirtyBits::CHILDREN, "first update must be complete");
                self.nodes.insert(
                    update.id,
                    MirrorNode { kind, parent: None, children: Vec::new(), layer: update.layer.clone() },
                );
            } else {
                let node =
                    self.nodes.get_mut(&update.id).unwrap_or_else(|| panic!("update for unknown {:?}", update.id));
                node.layer = update.layer.clone();
            }
        }
        assert!(self.pending.is_empty(), "realized layers without an initial update: {:?}", self.pending.keys());

        // 2. Children, in two passes so that moves between parents work in
        //    any order.
        for (parent, list) in &batch.children {
            let old =
                self.nodes.get(parent).unwrap_or_else(|| panic!("children for unknown {parent:?}")).children.clone();
            for c in old.iter().filter(|c| !list.contains(c)) {
                if let Some(n) = self.nodes.get_mut(c)
                    && n.parent == Some(*parent)
                {
                    n.parent = None;
                }
            }
        }
        for (parent, list) in &batch.children {
            for c in list {
                let node = self.nodes.get_mut(c).unwrap_or_else(|| panic!("{parent:?} lists unknown child {c:?}"));
                let previous = node.parent.replace(*parent);
                if let Some(prev) = previous
                    && prev != *parent
                    && let Some(p) = self.nodes.get_mut(&prev)
                {
                    p.children.retain(|x| x != c);
                }
            }
            self.nodes.get_mut(parent).unwrap().children = list.clone();
        }

        // 4. Release.
        for id in &batch.released {
            let node = self.nodes.remove(id).unwrap_or_else(|| panic!("release of unknown {id:?}"));
            assert_ne!(Some(*id), self.root, "the root was released");
            for c in &node.children {
                if let Some(n) = self.nodes.get_mut(c)
                    && n.parent == Some(*id)
                {
                    n.parent = None;
                }
            }
            if let Some(p) = node.parent.and_then(|p| self.nodes.get_mut(&p)) {
                p.children.retain(|x| x != id);
            }
        }

        self.check_invariants();
    }

    #[track_caller]
    fn check_invariants(&self) {
        for (id, node) in &self.nodes {
            for c in &node.children {
                let child = self.nodes.get(c).unwrap_or_else(|| panic!("{id:?} has dead child {c:?}"));
                assert_eq!(child.parent, Some(*id), "{c:?} is listed under {id:?} but parented elsewhere");
            }
            if let Some(m) = node.layer.mask {
                assert!(self.nodes.contains_key(&m), "{id:?} is masked by unknown {m:?}");
            }
        }
    }
}
