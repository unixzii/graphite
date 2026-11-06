//! The demo's scenes. Platform independent: only the graphite API is used.

mod images;
pub mod showcase;
pub mod ui;
mod util;

use graphite::{Scene, Size};

/// A scene's animation.
pub trait DemoScene {
    /// Poses the scene for time `t`, in seconds.
    fn update(&mut self, t: f64);
}

/// The scenes `--scene` can select.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SceneKind {
    /// Every supported feature, nearly the whole window animating.
    Showcase,
    /// A typical application window with a few small animated regions.
    Ui,
}

impl SceneKind {
    pub fn parse(name: &str) -> Option<SceneKind> {
        match name {
            "showcase" => Some(SceneKind::Showcase),
            "ui" => Some(SceneKind::Ui),
            _ => None,
        }
    }

    pub fn size(self) -> Size {
        match self {
            SceneKind::Showcase => showcase::SIZE,
            SceneKind::Ui => ui::SIZE,
        }
    }

    /// Populates `scene`, which must have this kind's size.
    pub fn build(self, scene: &Scene) -> Box<dyn DemoScene> {
        match self {
            SceneKind::Showcase => Box::new(showcase::Showcase::build(scene)),
            SceneKind::Ui => Box::new(ui::Ui::build(scene)),
        }
    }
}
