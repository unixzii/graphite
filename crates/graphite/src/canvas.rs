//! Immediate-mode drawing, CGContext style.
//!
//! [`Canvas`] does not rasterize anything: it records a [`DisplayList`] that a
//! backend later replays into CoreGraphics, Direct2D or Canvas2D, or
//! translates into GSK render nodes. Recording (instead of drawing directly)
//! lets backends re-rasterize at a new scale, rebuild after device loss, and
//! cross the WASM boundary in a single call.
//!
//! The op set is the intersection every backend implements exactly. Blend
//! modes and canvas shadows are intentionally absent (Direct2D cannot match
//! CoreGraphics there).

use std::sync::Arc;

use crate::geometry::{Affine, Rect};
use crate::resource::{FillRule, Image, Paint, Path, ResourceId, Stroke};

/// One recorded drawing command. Coordinates are in the canvas' user space
/// at the time of recording (the current transform is an op of its own).
#[derive(Clone, Debug, PartialEq)]
pub enum DrawOp {
    Save,
    Restore,
    ConcatTransform(Affine),
    /// Multiplies the current global alpha.
    SetAlpha(f32),
    ClipRect(Rect),
    ClipPath {
        path: Path,
        rule: FillRule,
    },
    FillPath {
        path: Path,
        rule: FillRule,
        paint: Paint,
    },
    StrokePath {
        path: Path,
        stroke: Stroke,
    },
    DrawImage {
        image: Image,
        dst: Rect,
    },
}

/// An immutable, recorded sequence of [`DrawOp`]s. `Save`/`Restore` are
/// always balanced.
#[derive(Clone)]
pub struct DisplayList(Arc<DisplayListInner>);

struct DisplayListInner {
    id: ResourceId,
    ops: Box<[DrawOp]>,
}

impl DisplayList {
    pub fn id(&self) -> ResourceId {
        self.0.id
    }

    pub fn ops(&self) -> &[DrawOp] {
        &self.0.ops
    }

    pub fn is_empty(&self) -> bool {
        self.0.ops.is_empty()
    }

    /// Records a display list with a free-standing canvas.
    pub fn record(f: impl FnOnce(&mut Canvas)) -> DisplayList {
        let mut canvas = Canvas::new();
        f(&mut canvas);
        canvas.finish()
    }
}

impl PartialEq for DisplayList {
    fn eq(&self, other: &DisplayList) -> bool {
        Arc::ptr_eq(&self.0, &other.0)
    }
}

impl std::fmt::Debug for DisplayList {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("DisplayList").field("id", &self.0.id).field("ops", &self.0.ops).finish()
    }
}

/// Records drawing commands into a [`DisplayList`].
pub struct Canvas {
    ops: Vec<DrawOp>,
    depth: usize,
}

impl Canvas {
    pub(crate) fn new() -> Self {
        Canvas { ops: Vec::new(), depth: 0 }
    }

    /// Pushes a copy of the graphics state (transform, clip, alpha).
    pub fn save(&mut self) {
        self.depth += 1;
        self.ops.push(DrawOp::Save);
    }

    /// Pops the graphics state. Unbalanced restores are ignored.
    pub fn restore(&mut self) {
        if self.depth > 0 {
            self.depth -= 1;
            self.ops.push(DrawOp::Restore);
        }
    }

    /// Runs `f` between a `save` and a `restore`.
    pub fn with_saved(&mut self, f: impl FnOnce(&mut Canvas)) {
        self.save();
        f(self);
        self.restore();
    }

    pub fn concat_transform(&mut self, m: Affine) {
        if !m.is_identity() {
            self.ops.push(DrawOp::ConcatTransform(m));
        }
    }

    pub fn set_alpha(&mut self, alpha: f32) {
        self.ops.push(DrawOp::SetAlpha(alpha.clamp(0.0, 1.0)));
    }

    pub fn clip_rect(&mut self, r: Rect) {
        self.ops.push(DrawOp::ClipRect(r));
    }

    pub fn clip_path(&mut self, path: &Path, rule: FillRule) {
        self.ops.push(DrawOp::ClipPath { path: path.clone(), rule });
    }

    pub fn fill_path(&mut self, path: &Path, rule: FillRule, paint: impl Into<Paint>) {
        self.ops.push(DrawOp::FillPath { path: path.clone(), rule, paint: paint.into() });
    }

    pub fn fill_rect(&mut self, r: Rect, paint: impl Into<Paint>) {
        self.fill_path(&Path::rect(r), FillRule::NonZero, paint);
    }

    pub fn stroke_path(&mut self, path: &Path, stroke: &Stroke) {
        self.ops.push(DrawOp::StrokePath { path: path.clone(), stroke: stroke.clone() });
    }

    pub fn draw_image(&mut self, image: &Image, dst: Rect) {
        self.ops.push(DrawOp::DrawImage { image: image.clone(), dst });
    }

    /// Closes any open `save`s and freezes the recording.
    pub(crate) fn finish(mut self) -> DisplayList {
        for _ in 0..self.depth {
            self.ops.push(DrawOp::Restore);
        }
        DisplayList(Arc::new(DisplayListInner { id: ResourceId::next(), ops: self.ops.into() }))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::color::Color;

    #[test]
    fn balances_save_restore() {
        let dl = DisplayList::record(|cx| {
            cx.restore(); // ignored
            cx.save();
            cx.save();
            cx.fill_rect(Rect::new(0.0, 0.0, 1.0, 1.0), Color::BLACK);
        });
        let saves = dl.ops().iter().filter(|op| **op == DrawOp::Save).count();
        let restores = dl.ops().iter().filter(|op| **op == DrawOp::Restore).count();
        assert_eq!((saves, restores), (2, 2));
        assert_eq!(dl.ops().first(), Some(&DrawOp::Save));
    }
}
