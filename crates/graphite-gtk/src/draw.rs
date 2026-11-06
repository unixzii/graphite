//! Translates a [`DisplayList`] into a GSK render node.
//!
//! Every op maps onto GPU-rendered GSK nodes; nothing goes through Cairo.
//! The canvas state machine maps onto `GtkSnapshot` like this:
//!
//! | DisplayList | GtkSnapshot |
//! |---|---|
//! | `Save` / `Restore` | `save` / `restore`, plus popping the clips pushed in between |
//! | `ConcatTransform` | `transform` |
//! | `ClipRect` | `push_clip` until the matching `Restore` |
//! | `ClipPath` | `push_mask` with a filled path as the mask |
//! | `SetAlpha` | tracked; each primitive is wrapped in `push_opacity` (CoreGraphics semantics: alpha applies per operation, not to a group) |
//! | `FillPath` / `StrokePath` | `append_fill` / `append_stroke`, or `push_fill` / `push_stroke` around a gradient node |
//! | `DrawImage` | `append_scaled_texture` |

use graphite::{DisplayList, DrawOp, Paint, Rect, Vec2};
use gtk::prelude::*;
use gtk::{graphene, gsk};

use crate::convert;

#[derive(Clone, Copy)]
struct State {
    alpha: f64,
    /// Pushed clip and mask nodes that the matching `Restore` must pop.
    pushes: usize,
}

/// Records `list` (in local space) as a node, or `None` if it draws nothing.
pub(crate) fn display_list_node(list: &DisplayList) -> Option<gsk::RenderNode> {
    let s = gtk::Snapshot::new();
    let mut stack = vec![State { alpha: 1.0, pushes: 0 }];
    for op in list.ops() {
        let top = *stack.last().unwrap();
        match op {
            DrawOp::Save => {
                s.save();
                stack.push(State { pushes: 0, ..top });
            }
            DrawOp::Restore => {
                if stack.len() > 1 {
                    for _ in 0..top.pushes {
                        s.pop();
                    }
                    stack.pop();
                    s.restore();
                }
            }
            DrawOp::ConcatTransform(m) => s.transform(convert::transform(*m).as_ref()),
            DrawOp::SetAlpha(a) => stack.last_mut().unwrap().alpha *= *a as f64,
            DrawOp::ClipRect(r) => {
                s.push_clip(&convert::rect(*r));
                stack.last_mut().unwrap().pushes += 1;
            }
            DrawOp::ClipPath { path, rule } => {
                s.push_mask(gsk::MaskMode::Alpha);
                s.append_fill(&convert::path(path, Vec2::ZERO), convert::fill_rule(*rule), &gtk::gdk::RGBA::BLACK);
                s.pop();
                stack.last_mut().unwrap().pushes += 1;
            }
            DrawOp::FillPath { path, rule, paint } => with_alpha(&s, top.alpha, |s| {
                let path = convert::path(path, Vec2::ZERO);
                let rule = convert::fill_rule(*rule);
                match paint {
                    Paint::Solid(c) => s.append_fill(&path, rule, &convert::rgba(*c)),
                    gradient => {
                        let Some(bounds) = path.bounds() else { return };
                        s.push_fill(&path, rule);
                        append_paint(s, gradient, &bounds);
                        s.pop();
                    }
                }
            }),
            DrawOp::StrokePath { path, stroke } => with_alpha(&s, top.alpha, |s| {
                let path = convert::path(path, Vec2::ZERO);
                let style = convert::stroke(&stroke.style);
                match &stroke.paint {
                    Paint::Solid(c) => s.append_stroke(&path, &style, &convert::rgba(*c)),
                    gradient => {
                        let Some(bounds) = path.stroke_bounds(&style) else { return };
                        s.push_stroke(&path, &style);
                        append_paint(s, gradient, &bounds);
                        s.pop();
                    }
                }
            }),
            DrawOp::DrawImage { image, dst } => with_alpha(&s, top.alpha, |s| {
                s.append_scaled_texture(&convert::texture(image), gsk::ScalingFilter::Linear, &convert::rect(*dst));
            }),
        }
    }
    // `Canvas` balances Save/Restore, but pop whatever the top level pushed.
    for _ in 0..stack[0].pushes {
        s.pop();
    }
    s.to_node()
}

fn with_alpha(s: &gtk::Snapshot, alpha: f64, f: impl FnOnce(&gtk::Snapshot)) {
    if alpha >= 1.0 {
        f(s);
    } else {
        s.push_opacity(alpha);
        f(s);
        s.pop();
    }
}

/// Appends a gradient paint covering `bounds`.
pub(crate) fn append_paint(s: &gtk::Snapshot, paint: &Paint, bounds: &graphene::Rect) {
    match paint {
        Paint::Solid(c) => s.append_color(&convert::rgba(*c), bounds),
        Paint::Linear { start, end, stops } => s.append_linear_gradient(
            bounds,
            &convert::point(*start),
            &convert::point(*end),
            &convert::color_stops(stops),
        ),
        Paint::Radial { center, radius, stops } => s.append_radial_gradient(
            bounds,
            &convert::point(*center),
            *radius as f32,
            *radius as f32,
            0.0,
            1.0,
            &convert::color_stops(stops),
        ),
    }
}

/// The local-space bounds a drawing is clipped to, like a bitmap backing
/// store the size of the layer.
pub(crate) fn clip_to(bounds: Rect) -> graphene::Rect {
    convert::rect(bounds)
}
