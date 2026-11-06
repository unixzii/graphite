//! Rendering a scene without showing it, for tests and thumbnails.
//!
//! Composition renders in the system compositor (DWM), so pixels can only
//! be read back from there. Windows.Graphics.Capture would be the natural
//! tool, but on the virtualized GPU this backend was developed on it
//! delivers empty frames. Instead, the scene is shown in a popup window
//! placed far outside the desktop, and read back with `PrintWindow`
//! (`PW_RENDERFULLCONTENT` asks DWM for the window's composed content, and
//! works for windows that are off-screen or covered).
//!
//! A window has no alpha channel, so every capture renders the scene twice,
//! over black and over white: with premultiplied colors, the result over
//! black is the color itself, and white minus black is `1 - alpha`.

use graphite::Scene;
use windows::Win32::Foundation::{HWND, RECT};
use windows::Win32::Graphics::Gdi::{
    BI_RGB, BITMAPINFO, BITMAPINFOHEADER, CreateCompatibleDC, CreateDIBSection, DIB_RGB_COLORS, DeleteDC, DeleteObject,
    GetDC, ReleaseDC, SelectObject,
};
use windows::Win32::Storage::Xps::{PRINT_WINDOW_FLAGS, PrintWindow};
use windows::Win32::UI::WindowsAndMessaging::GetWindowRect;
use windows::core::Result;

use crate::context::Context;
use crate::host::{self, SceneHost};

/// `PW_RENDERFULLCONTENT`: include DirectComposition content.
const PW_RENDERFULLCONTENT: PRINT_WINDOW_FLAGS = PRINT_WINDOW_FLAGS(2);

/// Premultiplied RGBA8 pixels, row 0 at the top.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Pixels {
    pub width: u32,
    pub height: u32,
    pub data: Vec<u8>,
}

impl Pixels {
    /// The pixel at `(x, y)`.
    pub fn at(&self, x: u32, y: u32) -> [u8; 4] {
        let i = ((y * self.width + x) * 4) as usize;
        [self.data[i], self.data[i + 1], self.data[i + 2], self.data[i + 3]]
    }
}

/// A scene presented in an off-screen window, ready to be captured.
pub struct OffscreenHost {
    host: SceneHost,
    width: u32,
    height: u32,
}

/// Presents `scene` off-screen at `scale` device pixels per point, sized to
/// its root layer, and sets the scene's backing scale. Commit the scene
/// yourself before capturing.
///
/// # Panics
/// If the scene already has a backend.
pub fn attach(scene: &Scene, scale: f64) -> Result<OffscreenHost> {
    let size = scene.root().bounds().size;
    let width = (size.width * scale).ceil().max(1.0) as u32;
    let height = (size.height * scale).ceil().max(1.0) as u32;
    let host = host::attach_offscreen(scene, width, height, scale)?;
    Ok(OffscreenHost { host, width, height })
}

impl OffscreenHost {
    pub fn host(&self) -> &SceneHost {
        &self.host
    }

    /// Reads back what the window shows, as premultiplied RGBA.
    pub fn capture(&self) -> Result<Pixels> {
        let hwnd = self.host.hwnd().expect("offscreen hosts have a window");
        let black = self.capture_over(hwnd, 0)?;
        let white = self.capture_over(hwnd, 255)?;
        let mut data = vec![0u8; black.len()];
        let (black, white) = (black.as_chunks::<4>().0, white.as_chunks::<4>().0);
        for ((out, b), w) in data.as_chunks_mut::<4>().0.iter_mut().zip(black).zip(white) {
            // BGRA rows; alpha from the mean difference of the channels.
            let diff: u32 = (0..3).map(|i| w[i].saturating_sub(b[i]) as u32).sum();
            let alpha = 255 - ((diff + 1) / 3).min(255) as u8;
            out[0] = b[2].min(alpha);
            out[1] = b[1].min(alpha);
            out[2] = b[0].min(alpha);
            out[3] = alpha;
        }
        Ok(Pixels { width: self.width, height: self.height, data })
    }

    /// The window's BGRA pixels with the backdrop set to `gray`.
    fn capture_over(&self, hwnd: HWND, gray: u8) -> Result<Vec<u8>> {
        let ctx = self.host.context();
        let backdrop = self.host.backdrop().expect("offscreen hosts have a backdrop");
        let color = windows::UI::Color { A: 255, R: gray, G: gray, B: gray };
        backdrop.SetBrush(&ctx.compositor.CreateColorBrushWithColor(color)?)?;
        ctx.wait_for_commit()?;
        print_window(hwnd, self.width, self.height)
    }
}

/// Reads back the composed contents of any window of this process (for
/// example a host created by [`crate::attach_to_hwnd`], or its parent) as
/// opaque RGBA, after waiting for composition changes to be committed.
/// Covered and off-screen windows work; hidden ones read back black.
pub fn capture_window(hwnd: HWND) -> Result<Pixels> {
    let mut rect = RECT::default();
    // SAFETY: any window handle is accepted.
    unsafe { GetWindowRect(hwnd, &mut rect)? };
    let (width, height) = ((rect.right - rect.left).max(1) as u32, (rect.bottom - rect.top).max(1) as u32);
    Context::get().wait_for_commit()?;
    let mut data = print_window(hwnd, width, height)?;
    for px in data.as_chunks_mut::<4>().0 {
        px.swap(0, 2);
        px[3] = 255;
    }
    Ok(Pixels { width, height, data })
}

/// `hwnd`'s pixels as top-down BGRA rows (the fourth byte is undefined).
fn print_window(hwnd: HWND, width: u32, height: u32) -> Result<Vec<u8>> {
    let (w, h) = (width as i32, height as i32);
    // SAFETY: GDI objects are created, used and released here; the DIB
    // section's bits stay valid until it is deleted.
    unsafe {
        let screen = GetDC(None);
        let dc = CreateCompatibleDC(Some(screen));
        let info = BITMAPINFO {
            bmiHeader: BITMAPINFOHEADER {
                biSize: size_of::<BITMAPINFOHEADER>() as u32,
                biWidth: w,
                biHeight: -h, // top-down
                biPlanes: 1,
                biBitCount: 32,
                biCompression: BI_RGB.0,
                ..Default::default()
            },
            ..Default::default()
        };
        let mut bits = std::ptr::null_mut();
        let bitmap = CreateDIBSection(Some(dc), &info, DIB_RGB_COLORS, &mut bits, None, 0);
        let result = bitmap.and_then(|bitmap| {
            let old = SelectObject(dc, bitmap.into());
            let ok = PrintWindow(hwnd, dc, PW_RENDERFULLCONTENT).as_bool();
            let pixels = std::slice::from_raw_parts(bits as *const u8, (w * h * 4) as usize).to_vec();
            SelectObject(dc, old);
            let _ = DeleteObject(bitmap.into());
            if ok { Ok(pixels) } else { Err(windows::core::Error::from_thread()) }
        });
        let _ = DeleteDC(dc);
        ReleaseDC(None, screen);
        result
    }
}

/// Attaches `scene` off-screen, commits it and captures it once.
///
/// # Panics
/// If the scene already has a backend.
pub fn render(scene: &Scene, scale: f64) -> Result<Pixels> {
    let host = attach(scene, scale)?;
    scene.commit();
    host.capture()
}
