//! Window-less rendering of a [`SceneHost`] with a GSK renderer, for tests
//! and thumbnails. Needs an initialized GTK (a display connection).

use gtk::prelude::*;
use gtk::{gdk, graphene, gsk};

use crate::SceneHost;

/// Which GSK renderer to render with.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RendererKind {
    /// The GPU renderer on OpenGL (`GskGLRenderer`).
    Gl,
    /// The GPU renderer on Vulkan (`GskVulkanRenderer`).
    Vulkan,
    /// The CPU fallback (`GskCairoRenderer`).
    Cairo,
}

impl RendererKind {
    fn create(self) -> gsk::Renderer {
        match self {
            RendererKind::Gl => gsk::GLRenderer::new().upcast(),
            RendererKind::Vulkan => gsk::VulkanRenderer::new().upcast(),
            RendererKind::Cairo => gsk::CairoRenderer::new().upcast(),
        }
    }
}

/// Premultiplied RGBA8 pixels, row 0 at the top.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Pixels {
    pub width: u32,
    pub height: u32,
    pub data: Vec<u8>,
}

/// A realized GSK renderer for offscreen rendering. Realizing is costly:
/// keep one and reuse it for every frame.
pub struct Renderer {
    renderer: gsk::Renderer,
}

impl Renderer {
    /// Realizes a renderer of `kind` for the default display.
    pub fn new(kind: RendererKind) -> Result<Renderer, gtk::glib::Error> {
        let display = gdk::Display::default().expect("GTK must be initialized with a display");
        let renderer = kind.create();
        renderer.realize_for_display(&display)?;
        Ok(Renderer { renderer })
    }

    /// Renders the host's last committed state at `scale` device pixels per
    /// point: `round(size · scale)` pixels for the root layer's size.
    pub fn render(&self, host: &SceneHost, scale: f64) -> Pixels {
        let size = host.size().unwrap_or_default();
        let (w, h) = ((size.width * scale).round() as u32, (size.height * scale).round() as u32);
        let snapshot = gtk::Snapshot::new();
        snapshot.scale(scale as f32, scale as f32);
        if let Some(node) = host.root_node() {
            snapshot.append_node(&node);
        }
        let viewport = graphene::Rect::new(0.0, 0.0, w as f32, h as f32);
        // An empty scene still renders as a transparent frame.
        let node =
            snapshot.to_node().unwrap_or_else(|| gsk::ColorNode::new(&gdk::RGBA::TRANSPARENT, &viewport).upcast());
        let texture = self.renderer.render_texture(&node, Some(&viewport));

        let mut downloader = gdk::TextureDownloader::new(&texture);
        downloader.set_format(gdk::MemoryFormat::R8g8b8a8Premultiplied);
        let (bytes, stride) = downloader.download_bytes();
        let row = w as usize * 4;
        let mut data = Vec::with_capacity(row * h as usize);
        for y in 0..h as usize {
            data.extend_from_slice(&bytes[y * stride..y * stride + row]);
        }
        Pixels { width: w, height: h, data }
    }
}

impl Drop for Renderer {
    fn drop(&mut self) {
        self.renderer.unrealize();
    }
}

/// Renders once with a temporary renderer; see [`Renderer::render`].
pub fn render(host: &SceneHost, kind: RendererKind, scale: f64) -> Result<Pixels, gtk::glib::Error> {
    Ok(Renderer::new(kind)?.render(host, scale))
}
