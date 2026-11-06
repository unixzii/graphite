//! Semantics of the model layer, observed through the recording backend.
//! Every commit is also replayed into a `MirrorTree`, which asserts the
//! `CommitBatch` contract.

use std::cell::Cell;
use std::rc::Rc;

use graphite::testing::Recorder;
use graphite::*;

fn scene() -> (Scene, Recorder) {
    let scene = Scene::new(Size::new(400.0, 300.0));
    let rec = Recorder::new();
    scene.set_backend(rec.backend());
    (scene, rec)
}

fn approx(a: Point, b: Point) -> bool {
    (a.x - b.x).abs() < 1e-9 && (a.y - b.y).abs() < 1e-9
}

// ───────────────────────────── commit basics ─────────────────────────────

#[test]
fn first_commit_realizes_the_root() {
    let (scene, rec) = scene();
    assert_eq!(rec.commit_requests(), 1, "a fresh scene is dirty");
    scene.commit();
    let root = scene.root().id();
    let batch = rec.last();
    assert_eq!(batch.root, Some(root));
    assert_eq!(batch.realized, vec![(root, LayerKind::Container)]);
    assert_eq!(batch.updates[0].layer.transform, Affine::IDENTITY);
    assert_eq!(batch.updates[0].layer.props.bounds, Rect::new(0.0, 0.0, 400.0, 300.0));
}

#[test]
fn commit_is_requested_once_until_committed() {
    let (scene, rec) = scene();
    scene.commit();
    let layer = Layer::new(&scene);
    scene.root().add_sublayer(&layer);
    layer.set_opacity(0.5);
    layer.set_hidden(true);
    assert_eq!(rec.commit_requests(), 2);
    scene.commit();
    assert!(!scene.needs_commit());
    layer.set_opacity(0.25);
    assert_eq!(rec.commit_requests(), 3);
}

#[test]
fn nothing_is_committed_without_changes() {
    let (scene, rec) = scene();
    scene.commit();
    scene.commit();
    assert_eq!(rec.batches().len(), 1);
}

#[test]
fn changes_coalesce_and_no_op_sets_stay_clean() {
    let (scene, rec) = scene();
    let layer = Layer::new(&scene);
    scene.root().add_sublayer(&layer);
    scene.commit();

    layer.set_opacity(0.5);
    layer.set_opacity(0.3);
    layer.set_position(Point::new(5.0, 5.0));
    scene.commit();
    let batch = rec.last();
    assert_eq!(batch.updates.len(), 1);
    assert_eq!(batch.updates[0].changed, DirtyBits::APPEARANCE | DirtyBits::GEOMETRY);
    assert_eq!(batch.updates[0].layer.props.opacity, 0.3);
    drop(batch);

    layer.set_opacity(0.3);
    assert!(!scene.needs_commit());
}

#[test]
fn size_changes_dirty_dependent_groups() {
    let (scene, rec) = scene();
    let image = Image::from_rgba8_premultiplied(2, 2, 1.0, vec![0u8; 16]);
    let content = ContentLayer::new(&scene);
    content.set_contents(Contents::Image(image));
    content.set_gravity(Gravity::Center);
    content.set_shadow(Some(Shadow::new(Color::BLACK, Vec2::ZERO, 2.0)));
    scene.root().add_sublayer(&content);
    scene.commit();

    content.set_position(Point::new(1.0, 1.0));
    scene.commit();
    assert_eq!(rec.last().updates[0].changed, DirtyBits::GEOMETRY);

    content.set_bounds(Rect::new(0.0, 0.0, 10.0, 10.0));
    scene.commit();
    let batch = rec.last();
    let update = &batch.updates[0];
    assert_eq!(update.changed, DirtyBits::GEOMETRY | DirtyBits::SHADOW | DirtyBits::CONTENT);
    match &update.layer.content {
        ResolvedContent::Image { rect, .. } => assert_eq!(*rect, Rect::new(4.0, 4.0, 2.0, 2.0)),
        other => panic!("unexpected content {other:?}"),
    }
    assert_eq!(
        update.layer.shadow_shape,
        Some(ShadowShape::RoundedRect {
            rect: Rect::new(0.0, 0.0, 10.0, 10.0),
            radius: 0.0,
            corners: Corners::ALL,
            spread: 0.0
        })
    );
}

#[test]
fn resolved_transform_follows_the_calayer_model() {
    let (scene, rec) = scene();
    let layer = Layer::new(&scene);
    layer.set_bounds(Rect::new(10.0, 20.0, 100.0, 50.0));
    layer.set_position(Point::new(200.0, 100.0)); // anchor (0.5, 0.5) lands here
    layer.set_transform(Affine::scale(2.0, 2.0));
    scene.root().add_sublayer(&layer);
    scene.commit();

    let batch = rec.last();
    let resolved = &batch.updates.iter().find(|u| u.id == layer.id()).unwrap().layer;
    // The box's center maps to `position`, scaled around it.
    assert!(approx(resolved.transform.transform_point(Point::new(50.0, 25.0)), Point::new(200.0, 100.0)));
    assert!(approx(resolved.transform.transform_point(Point::new(0.0, 0.0)), Point::new(100.0, 50.0)));
    assert_eq!(resolved.bounds_offset, Vec2::new(-10.0, -20.0));
    assert_eq!(layer.frame(), Rect::new(100.0, 50.0, 200.0, 100.0));
}

#[test]
fn set_frame_round_trips_without_transform() {
    let (scene, _rec) = scene();
    let layer = Layer::new(&scene);
    layer.set_anchor_point(Point::new(0.0, 1.0));
    layer.set_frame(Rect::new(10.0, 20.0, 30.0, 40.0));
    assert_eq!(layer.position(), Point::new(10.0, 60.0));
    assert_eq!(layer.frame(), Rect::new(10.0, 20.0, 30.0, 40.0));
}

// ───────────────────────────── lifetime ─────────────────────────────

#[test]
fn sublayers_outlive_their_local_handles() {
    let (scene, rec) = scene();
    {
        let card = Layer::new(&scene);
        let ring = ShapeLayer::new(&scene);
        card.add_sublayer(&ring);
        scene.root().add_sublayer(&card);
    } // both local handles dropped here
    assert_eq!(scene.layer_count(), 3);
    scene.commit();

    let root = scene.root();
    let card = &root.sublayers()[0];
    assert_eq!(card.sublayers()[0].kind(), LayerKind::Shape);
    let tree = rec.tree();
    assert_eq!(tree.children(root.id()), &[card.id()]);
    assert_eq!(tree.len(), 3);
}

#[test]
fn unreferenced_layers_are_freed_and_released() {
    let (scene, rec) = scene();
    let card = Layer::new(&scene);
    card.add_sublayer(&Layer::new(&scene));
    scene.root().add_sublayer(&card);
    scene.commit();
    let card_id = card.id();
    let child_id = card.sublayers()[0].id();

    card.remove_from_superlayer();
    assert_eq!(scene.layer_count(), 3, "our handle keeps the subtree alive");
    drop(card);
    assert_eq!(scene.layer_count(), 1);

    scene.commit();
    let batch = rec.last();
    assert_eq!(batch.children, vec![(scene.root().id(), vec![])]);
    let mut released = batch.released.clone();
    released.sort();
    let mut expected = vec![card_id, child_id];
    expected.sort();
    assert_eq!(released, expected);
    drop(batch);
    assert_eq!(rec.tree().len(), 1);
}

#[test]
fn layers_freed_before_their_first_commit_never_reach_the_backend() {
    let (scene, rec) = scene();
    scene.commit();
    let layer = Layer::new(&scene);
    scene.root().add_sublayer(&layer);
    layer.remove_from_superlayer();
    drop(layer);
    scene.commit();
    let batch = rec.last();
    assert!(batch.realized.is_empty() && batch.released.is_empty());
}

#[test]
fn detached_layers_keep_their_native_object() {
    let (scene, rec) = scene();
    let layer = Layer::new(&scene);
    scene.root().add_sublayer(&layer);
    scene.commit();

    layer.remove_from_superlayer();
    layer.set_opacity(0.5); // not committed while detached
    scene.commit();
    {
        let batch = rec.last();
        assert!(batch.updates.is_empty());
        assert!(batch.released.is_empty());
        assert_eq!(rec.tree().node(layer.id()).unwrap().parent, None);
    }

    scene.root().add_sublayer(&layer);
    scene.commit();
    let batch = rec.last();
    assert!(batch.realized.is_empty(), "re-attaching does not realize again");
    assert_eq!(batch.updates.len(), 1);
    assert_eq!(batch.updates[0].changed, DirtyBits::APPEARANCE);
}

#[test]
fn moving_between_parents_in_one_commit() {
    let (scene, rec) = scene();
    let a = Layer::new(&scene);
    let b = Layer::new(&scene);
    let child = Layer::new(&scene);
    scene.root().add_sublayer(&a);
    scene.root().add_sublayer(&b);
    a.add_sublayer(&child);
    scene.commit();

    b.add_sublayer(&child);
    scene.commit();
    let tree = rec.tree();
    assert!(tree.children(a.id()).is_empty());
    assert_eq!(tree.children(b.id()), &[child.id()]);
}

#[test]
fn insert_and_reorder_within_a_parent() {
    let (scene, rec) = scene();
    let root = scene.root();
    let (x, y, z) = (Layer::new(&scene), Layer::new(&scene), Layer::new(&scene));
    root.add_sublayer(&x);
    root.add_sublayer(&y);
    root.insert_sublayer(&z, 0);
    assert_eq!(root.sublayers(), vec![z.clone(), x.clone(), y.clone()]);
    root.add_sublayer(&z); // re-adding moves it to the top
    assert_eq!(root.sublayers(), vec![x.clone(), y.clone(), z.clone()]);
    root.insert_sublayer(&y, 99); // clamped
    scene.commit();
    assert_eq!(rec.tree().children(root.id()), &[x.id(), z.id(), y.id()]);
}

#[test]
fn weak_handles_do_not_keep_layers_alive() {
    let (scene, _rec) = scene();
    let layer = Layer::new(&scene);
    let weak = layer.downgrade();
    assert_eq!(weak.upgrade(), Some(layer.clone()));
    drop(layer);
    assert!(weak.upgrade().is_none());
}

#[test]
fn dropping_the_scene_turns_handles_inert() {
    let (scene, _rec) = scene();
    let layer = Layer::new(&scene);
    layer.set_opacity(0.5);
    drop(scene);
    assert!(!layer.is_alive());
    layer.set_opacity(0.1); // no-op
    assert_eq!(layer.opacity(), 1.0, "getters return defaults");
    assert!(layer.sublayers().is_empty());
    let other = layer.clone();
    drop(other);
}

#[test]
fn typed_handles_deref_and_downcast() {
    let (scene, _rec) = scene();
    let shape = ShapeLayer::new(&scene);
    shape.set_opacity(0.5); // base method through Deref
    scene.root().add_sublayer(&shape);

    let any: Layer = scene.root().sublayers().remove(0);
    assert!(any.downcast_ref::<GradientLayer>().is_none());
    assert_eq!(any.downcast_ref::<ShapeLayer>(), Some(&shape));
    let back: ShapeLayer = any.downcast().unwrap();
    assert_eq!(back.opacity(), 0.5);
    let erased: Layer = back.into();
    assert!(erased.downcast::<ContentLayer>().is_err());
}

// ───────────────────────────── invalid trees ─────────────────────────────

#[test]
#[should_panic(expected = "cycle")]
fn adding_an_ancestor_panics() {
    let (scene, _rec) = scene();
    let a = Layer::new(&scene);
    let b = Layer::new(&scene);
    a.add_sublayer(&b);
    b.add_sublayer(&a);
}

#[test]
#[should_panic(expected = "cycle")]
fn adding_self_panics() {
    let (scene, _rec) = scene();
    let a = Layer::new(&scene);
    a.add_sublayer(&a);
}

#[test]
#[should_panic(expected = "different scenes")]
fn mixing_scenes_panics() {
    let (one, _r1) = scene();
    let (two, _r2) = scene();
    one.root().add_sublayer(&Layer::new(&two));
}

#[test]
#[should_panic(expected = "root layer cannot be added")]
fn adding_the_root_panics() {
    let (scene, _rec) = scene();
    Layer::new(&scene).add_sublayer(&scene.root());
}

// ───────────────────────────── masks ─────────────────────────────

#[test]
fn masks_are_realized_with_their_owner() {
    let (scene, rec) = scene();
    let owner = Layer::new(&scene);
    let mask = ShapeLayer::new(&scene);
    owner.set_mask(Some(&mask));
    scene.root().add_sublayer(&owner);
    let mask_id = mask.id();
    drop(mask);
    assert_eq!(owner.mask().map(|m| m.id()), Some(mask_id), "the owner keeps its mask alive");
    scene.commit();
    {
        let tree = rec.tree();
        assert!(tree.contains(mask_id));
        assert_eq!(tree.node(owner.id()).unwrap().layer.mask, Some(mask_id));
    }

    owner.set_mask(None);
    scene.commit();
    let batch = rec.last();
    assert_eq!(batch.released, vec![mask_id]);
    assert_eq!(batch.updates[0].changed, DirtyBits::MASK);
}

#[test]
#[should_panic(expected = "must not have a superlayer")]
fn a_layer_in_the_tree_cannot_be_a_mask() {
    let (scene, _rec) = scene();
    let a = Layer::new(&scene);
    let b = Layer::new(&scene);
    scene.root().add_sublayer(&b);
    a.set_mask(Some(&b));
}

#[test]
#[should_panic(expected = "nested masks")]
fn masks_cannot_be_nested() {
    let (scene, _rec) = scene();
    let (a, m1, m2) = (Layer::new(&scene), Layer::new(&scene), Layer::new(&scene));
    a.set_mask(Some(&m1));
    m1.set_mask(Some(&m2));
}

#[test]
#[should_panic(expected = "used as a mask")]
fn a_mask_cannot_be_added_as_a_sublayer() {
    let (scene, _rec) = scene();
    let (a, m) = (Layer::new(&scene), Layer::new(&scene));
    a.set_mask(Some(&m));
    scene.root().add_sublayer(&m);
}

// ───────────────────────────── drawing ─────────────────────────────

fn drawn_version(rec: &Recorder, id: LayerId) -> u64 {
    match rec.tree().node(id).unwrap().layer.content {
        ResolvedContent::Drawn { version } => version,
        ref other => panic!("expected drawn content, got {other:?}"),
    }
}

#[test]
fn backends_run_drawers_when_they_need_the_drawing() {
    let (scene, rec) = scene();
    let runs = Rc::new(Cell::new(0));
    let label = ContentLayer::new(&scene);
    label.set_bounds(Rect::new(0.0, 0.0, 50.0, 20.0));
    let counter = runs.clone();
    label.set_drawer(move |cx, info| {
        counter.set(counter.get() + 1);
        cx.fill_rect(info.bounds, Color::BLACK);
    });
    assert_eq!(runs.get(), 0, "installing a drawer does not run it");
    assert_eq!(label.contents(), Contents::None, "drawings are not stored as contents");

    scene.commit();
    assert_eq!(runs.get(), 0, "detached layers are not committed, so not drawn");

    scene.root().add_sublayer(&label);
    scene.commit();
    assert_eq!(runs.get(), 1);
    let first = drawn_version(&rec, label.id());
    {
        let tree = rec.tree();
        assert!(matches!(tree.drawing(label.id()).unwrap().ops(), [DrawOp::FillPath { .. }]));
        let (_, info, _) = tree.node(label.id()).unwrap().drawing.as_ref().unwrap();
        assert_eq!(*info, DrawInfo { bounds: Rect::new(0.0, 0.0, 50.0, 20.0), scale: 1.0 });
    }

    scene.commit();
    label.set_opacity(0.5);
    scene.commit();
    assert_eq!(runs.get(), 1, "no redraw while version and draw info are unchanged");

    label.set_needs_display();
    label.set_needs_display();
    scene.commit();
    assert_eq!(runs.get(), 2);
    assert_eq!(rec.last().updates[0].changed, DirtyBits::CONTENT);
    assert!(drawn_version(&rec, label.id()) > first);

    let version = drawn_version(&rec, label.id());
    scene.set_backing_scale(2.0);
    scene.commit();
    assert_eq!(runs.get(), 3, "a scale change redraws");
    assert_eq!(drawn_version(&rec, label.id()), version, "…without a new version");

    label.set_bounds(Rect::new(0.0, 0.0, 60.0, 20.0));
    scene.commit();
    assert_eq!(runs.get(), 4, "a size change redraws");
    assert!(rec.last().updates[0].changed.contains(DirtyBits::CONTENT));
    label.set_bounds(Rect::new(5.0, 0.0, 60.0, 20.0));
    scene.commit();
    assert_eq!(runs.get(), 5, "so does a bounds origin change");
    assert!(rec.last().updates[0].changed.contains(DirtyBits::CONTENT));
}

#[test]
fn drawers_can_be_run_outside_commits() {
    let (scene, rec) = scene();
    let label = ContentLayer::new(&scene);
    label.set_bounds(Rect::new(0.0, 0.0, 10.0, 10.0));
    label.set_drawer(|cx, info| cx.fill_rect(info.bounds, Color::BLACK));
    scene.root().add_sublayer(&label);
    scene.commit();
    let drawers = rec.last().drawers.clone();
    let info = DrawInfo { bounds: Rect::new(0.0, 0.0, 4.0, 4.0), scale: 3.0 };

    let list = drawers.draw(label.id(), &info).expect("drawn on demand");
    assert!(!list.is_empty());
    assert!(drawers.draw(scene.root().id(), &info).is_none(), "a layer without a drawer");

    let id = label.id();
    label.clear_drawer();
    assert!(drawers.draw(id, &info).is_none(), "the drawer was removed");
    scene.commit();
    assert!(rec.tree().drawing(id).is_none(), "clearing the drawer clears the drawing");
    assert_eq!(rec.tree().node(id).unwrap().layer.content, ResolvedContent::None);

    label.remove_from_superlayer();
    drop(label);
    assert!(drawers.draw(id, &info).is_none(), "the layer is gone");
    drop(scene);
    assert!(drawers.draw(id, &info).is_none(), "the scene is gone");
}

#[test]
fn drawers_read_the_scene() {
    let (scene, rec) = scene();
    let label = ContentLayer::new(&scene);
    let weak = label.downgrade();
    label.set_opacity(0.25);
    label.set_drawer(move |cx, info| {
        let me = weak.upgrade().unwrap(); // a temporary strong handle is fine
        cx.set_alpha(me.opacity());
        cx.fill_rect(info.bounds, Color::BLACK);
    });
    scene.root().add_sublayer(&label);
    scene.commit();
    assert!(rec.tree().drawing(label.id()).is_some());
    assert!(!scene.needs_commit(), "reading does not dirty the scene");
}

#[test]
#[should_panic(expected = "a drawer must not modify the scene")]
fn drawers_must_not_modify_the_scene() {
    let (scene, _rec) = scene();
    let label = ContentLayer::new(&scene);
    let weak = label.downgrade();
    label.set_drawer(move |_cx, _info| {
        weak.upgrade().unwrap().set_opacity(0.5);
    });
    scene.root().add_sublayer(&label);
    scene.commit();
}

#[test]
fn drawers_and_contents_replace_each_other() {
    let (scene, rec) = scene();
    let layer = ContentLayer::new(&scene);
    scene.root().add_sublayer(&layer);
    let image = Image::from_rgba8_premultiplied(1, 1, 1.0, vec![255, 0, 0, 255]);
    layer.set_contents(Contents::Image(image));
    layer.set_drawer(|cx, info| cx.fill_rect(info.bounds, Color::BLACK));
    assert_eq!(layer.contents(), Contents::None, "a drawer replaces the contents");
    scene.commit();
    assert!(matches!(rec.tree().node(layer.id()).unwrap().layer.content, ResolvedContent::Drawn { .. }));

    layer.set_contents(Contents::None);
    assert!(!layer.has_drawer(), "contents replace the drawer");
    scene.commit();
    assert!(rec.tree().drawing(layer.id()).is_none());
}

#[test]
fn dropping_a_drawer_can_free_other_layers() {
    let (scene, rec) = scene();
    let owned = Layer::new(&scene); // only the drawer keeps this alive
    let owned_id = owned.id();
    let label = ContentLayer::new(&scene);
    label.add_sublayer(&owned);
    owned.remove_from_superlayer();
    label.set_drawer(move |_cx, _info| {
        let _keep = &owned;
    });
    scene.root().add_sublayer(&label);
    scene.commit();
    assert_eq!(scene.layer_count(), 3);

    // Replacing the drawer drops the closure, which drops `owned`, re-entering
    // the scene's release path.
    label.set_contents(Contents::None);
    assert_eq!(scene.layer_count(), 2);
    scene.commit();
    assert!(!rec.tree().contains(owned_id));
}

#[test]
fn freeing_cascades_through_drawers() {
    let (scene, _rec) = scene();
    let leaf = Layer::new(&scene);
    let holder = ContentLayer::new(&scene);
    holder.set_drawer(move |_cx, _info| {
        let _keep = &leaf;
    });
    let parent = Layer::new(&scene);
    parent.add_sublayer(&holder);
    drop(holder);
    assert_eq!(scene.layer_count(), 4);
    drop(parent); // frees parent → holder → its drawer → leaf
    assert_eq!(scene.layer_count(), 1);
}

#[test]
fn commit_without_backend_is_a_no_op() {
    let scene = Scene::new(Size::new(10.0, 10.0));
    scene.commit();
    assert!(scene.needs_commit());
    let rec = Recorder::new();
    scene.set_backend(rec.backend());
    assert_eq!(rec.commit_requests(), 1, "installing a backend requests the pending commit");
    scene.commit();
    assert_eq!(rec.tree().len(), 1);
}

#[test]
fn releasing_a_detached_layer_requests_a_commit() {
    let (scene, rec) = scene();
    let layer = Layer::new(&scene);
    scene.root().add_sublayer(&layer);
    scene.commit();
    layer.remove_from_superlayer();
    scene.commit();
    let requests = rec.commit_requests();

    let id = layer.id();
    drop(layer);
    assert_eq!(rec.commit_requests(), requests + 1);
    scene.commit();
    assert_eq!(rec.last().released, vec![id]);
}

// ───────────────────────────── pixel snapping ─────────────────────────────

fn resolved_of(rec: &Recorder, id: LayerId) -> Resolved {
    rec.tree().node(id).expect("realized").layer.clone()
}

#[test]
fn translations_snap_to_device_pixels() {
    let (scene, rec) = scene();
    let layer = Layer::new(&scene);
    layer.set_frame(Rect::new(10.3, 20.7, 10.0, 10.0));
    layer.set_bounds(Rect::new(0.4, 0.6, 10.0, 10.0));
    scene.root().add_sublayer(&layer);
    scene.commit();

    let r = resolved_of(&rec, layer.id());
    assert!(approx(r.transform.transform_point(Point::ZERO), Point::new(10.0, 21.0)));
    assert!((r.snap_offset.x + 0.3).abs() < 1e-9 && (r.snap_offset.y - 0.3).abs() < 1e-9);
    assert_eq!(r.bounds_offset, Vec2::new(0.0, -1.0));
    assert_eq!(r.props.position, Point::new(15.3, 25.7), "raw properties are untouched");

    scene.set_backing_scale(2.0);
    scene.commit();
    let batch = rec.last();
    assert!(batch.updates.iter().any(|u| u.id == layer.id() && u.changed.contains(DirtyBits::GEOMETRY)));
    drop(batch);
    let r = resolved_of(&rec, layer.id());
    assert!(approx(r.transform.transform_point(Point::ZERO), Point::new(10.5, 20.5)));
    assert_eq!(r.bounds_offset, Vec2::new(-0.5, -0.5));
}

#[test]
fn pixel_snapping_can_be_disabled() {
    let (scene, rec) = scene();
    let layer = Layer::new(&scene);
    layer.set_frame(Rect::new(10.3, 0.0, 10.0, 10.0));
    scene.root().add_sublayer(&layer);
    scene.commit();
    assert!(scene.pixel_snapping());

    scene.set_pixel_snapping(false);
    assert!(scene.needs_commit());
    scene.commit();
    let r = resolved_of(&rec, layer.id());
    assert!(approx(r.transform.transform_point(Point::ZERO), Point::new(10.3, 0.0)));
    assert_eq!(r.snap_offset, Vec2::ZERO);
}

#[test]
fn moving_a_custom_shadow_path_with_the_bounds_origin() {
    let (scene, rec) = scene();
    let layer = Layer::new(&scene);
    let mut shadow = Shadow::new(Color::BLACK, Vec2::ZERO, 1.0);
    shadow.path = Some(Path::rect(Rect::new(0.0, 0.0, 5.0, 5.0)));
    layer.set_shadow(Some(shadow));
    scene.root().add_sublayer(&layer);
    scene.commit();
    layer.set_bounds(Rect::new(3.0, 0.0, 0.0, 0.0));
    scene.commit();
    assert_eq!(rec.last().updates[0].changed, DirtyBits::GEOMETRY | DirtyBits::SHADOW);
}

#[test]
fn sizes_and_border_widths_snap_to_device_pixels() {
    let (scene, rec) = scene();
    scene.set_backing_scale(4.0 / 3.0);
    let layer = Layer::new(&scene);
    layer.set_frame(Rect::new(10.0, 10.0, 40.0, 20.0)); // 53.33 × 26.67 device pixels
    layer.set_border(Some(Border { width: 1.0, color: Color::BLACK }));
    let rotated = Layer::new(&scene);
    rotated.set_frame(Rect::new(0.0, 0.0, 40.0, 20.0));
    rotated.set_transform(Affine::rotate(0.3));
    scene.root().add_sublayer(&layer);
    scene.root().add_sublayer(&rotated);
    scene.commit();

    let s = 4.0 / 3.0;
    let r = resolved_of(&rec, layer.id());
    let px = |v: f64| v * s;
    assert!((px(r.size.width) - 53.0).abs() < 1e-9 && (px(r.size.height) - 27.0).abs() < 1e-9, "{:?}", r.size);
    let origin = r.transform.transform_point(Point::ZERO);
    let far = r.transform.transform_point(Point::new(r.size.width, r.size.height));
    for v in [origin.x, origin.y, far.x, far.y] {
        assert!((px(v) - px(v).round()).abs() < 1e-9, "edge {v} is not on the pixel grid");
    }
    assert_eq!(r.border.map(|b| px(b.width).round()), Some(1.0));
    assert_eq!(r.props.bounds.size, Size::new(40.0, 20.0), "raw properties are untouched");

    // Only axis-aligned boxes are resized.
    assert_eq!(resolved_of(&rec, rotated.id()).size, Size::new(40.0, 20.0));
}

#[test]
fn shadow_spread_grows_rounded_corners_only() {
    let (scene, rec) = scene();
    let layer = Layer::new(&scene);
    layer.set_frame(Rect::new(0.0, 0.0, 40.0, 20.0));
    layer.set_corner_radius(4.0);
    layer.set_masked_corners(Corners::TOP_LEFT);
    layer.set_shadow(Some(Shadow::new(Color::BLACK, Vec2::ZERO, 2.0).with_spread(3.0)));
    scene.root().add_sublayer(&layer);
    scene.commit();
    let shape = resolved_of(&rec, layer.id()).shadow_shape.unwrap();
    assert_eq!(
        shape,
        ShadowShape::RoundedRect {
            rect: Rect::new(0.0, 0.0, 40.0, 20.0),
            radius: 4.0,
            corners: Corners::TOP_LEFT,
            spread: 3.0
        }
    );
    assert_eq!(grow_rounded_rect(Rect::new(0.0, 0.0, 40.0, 20.0), 4.0, 3.0), (Rect::new(-3.0, -3.0, 46.0, 26.0), 7.0));
    assert_eq!(grow_rounded_rect(Rect::new(0.0, 0.0, 40.0, 20.0), 0.0, 3.0).1, 0.0, "square corners stay square");
    assert_eq!(grow_rounded_rect(Rect::new(0.0, 0.0, 4.0, 4.0), 1.0, -3.0), (Rect::new(2.0, 2.0, 0.0, 0.0), 0.0));
}
