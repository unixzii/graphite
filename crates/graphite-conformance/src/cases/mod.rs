//! The case registry.
//!
//! [`__with_case_names!`] is the single list of cases: it feeds both the
//! registry below and the `#[test]` functions [`crate::conformance_tests!`]
//! generates. Each entry is `<module> <function>`; the module decides the
//! tier.

mod measured;
mod semantic;

use graphite::{Color, Layer, Rect, Scene};

use crate::frame::Rgba8;

#[doc(hidden)]
#[macro_export]
macro_rules! __with_case_names {
    ($callback:ident; $($args:tt)*) => {
        $crate::$callback!($($args)*;
            semantic geometry_is_y_down
            semantic transform_rotates_around_the_anchor_point
            semantic skew_transforms_apply
            semantic z_order_follows_sublayer_order
            semantic hidden_hides_the_subtree
            semantic masks_to_bounds_clips_sublayers
            semantic masked_corners_round_only_selected_corners
            semantic border_is_inside_and_above_sublayers
            semantic opacity_is_group_opacity
            semantic masks_clip_by_alpha
            semantic bounds_origin_scrolls_sublayers_and_drawings
            semantic image_contents_are_upright
            semantic image_gravity_places_contents
            semantic drawings_are_upright
            semantic canvas_state_is_saved_and_restored
            semantic canvas_clip_and_alpha_apply
            semantic shapes_use_local_coordinates
            semantic stroke_end_trims_the_path
            semantic linear_gradient_unit_space_is_y_down
            semantic radial_gradient_is_centered
            semantic shadow_offset_is_y_down_and_escapes_masks_to_bounds
            semantic shadow_is_drawn_under_layers_without_background
            semantic fractional_positions_snap_to_device_pixels
            semantic updates_and_removals_reach_the_screen
            measured shadow_blur_matches_sigma
            measured shadow_blur_matches_sigma_without_background
            measured linear_gradient_interpolates_monotonically
            measured stroke_width_matches
            measured corner_radius_follows_a_circular_arc
            measured group_opacity_halves_alpha
        );
    };
}

#[doc(hidden)]
#[macro_export]
macro_rules! __conformance_registry {
    (; $($module:ident $name:ident)*) => {
        pub(crate) static ALL: &[$crate::Case] = &[
            $( $crate::Case { name: stringify!($name), tier: $module::TIER, run: $module::$name } ),*
        ];
    };
}

crate::__with_case_names!(__conformance_registry;);

// ── helpers shared by the cases ──

pub(crate) const RED: Rgba8 = [255, 0, 0, 255];
pub(crate) const BLUE: Rgba8 = [0, 0, 255, 255];
pub(crate) const WHITE: Rgba8 = [255, 255, 255, 255];
pub(crate) const BLACK: Rgba8 = [0, 0, 0, 255];
pub(crate) const CLEAR: Rgba8 = [0, 0, 0, 0];

pub(crate) fn red() -> Color {
    Color::rgb(1.0, 0.0, 0.0)
}

pub(crate) fn blue() -> Color {
    Color::rgb(0.0, 0.0, 1.0)
}

/// A plain layer with a background, added to the root.
pub(crate) fn solid(scene: &Scene, frame: Rect, color: Color) -> Layer {
    let layer = Layer::new(scene);
    layer.set_frame(frame);
    layer.set_background(Some(color));
    scene.root().add_sublayer(&layer);
    layer
}

#[cfg(test)]
mod tests {
    #[test]
    fn names_are_unique() {
        let mut names: Vec<_> = super::ALL.iter().map(|c| c.name).collect();
        names.sort();
        names.dedup();
        assert_eq!(names.len(), super::ALL.len());
    }
}
