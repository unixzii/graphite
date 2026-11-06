//! [`SceneHost`]: presents a [`Scene`] in a window or a visual tree.

use std::rc::Rc;

use graphite::Scene;
use windows::UI::Composition::Desktop::DesktopWindowTarget;
use windows::UI::Composition::{CompositionBorderMode, Compositor, ContainerVisual, SpriteVisual};
use windows::Win32::Foundation::{HWND, RECT};
use windows::Win32::System::WinRT::Composition::ICompositorDesktopInterop;
use windows::Win32::UI::HiDpi::{DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2, SetThreadDpiAwarenessContext};
use windows::Win32::UI::WindowsAndMessaging::{
    DestroyWindow, GetClientRect, WS_CHILD, WS_CLIPSIBLINGS, WS_EX_NOACTIVATE, WS_EX_NOREDIRECTIONBITMAP,
    WS_EX_TOOLWINDOW, WS_POPUP, WS_VISIBLE,
};
use windows::core::{Interface, Result};

use crate::backend::WinCompBackend;
use crate::context::Context;
use crate::convert::vec2;
use crate::window;

/// Where a [`Scene`] is presented.
///
/// A host owns a *host visual*, a `ContainerVisual` that fills whatever
/// contains it and clips to it. Its only child is a container scaled by the
/// backing scale (composition units are pixels), whose only child is the
/// scene's root layer, at the top-left corner.
///
/// Dropping a host detaches the scene: hosts created by [`attach_to_hwnd`]
/// destroy their window, hosts created by [`attach_to_visual`] remove the
/// host visual from its container. Keep the host alive while the scene is
/// shown.
pub struct SceneHost {
    visual: ContainerVisual,
    /// Below the scene, for offscreen read-back.
    backdrop: Option<SpriteVisual>,
    window: Option<(HWND, DesktopWindowTarget)>,
    ctx: Rc<Context>,
}

impl SceneHost {
    /// The host visual; the scaled container holding the scene is its child.
    pub fn visual(&self) -> &ContainerVisual {
        &self.visual
    }

    /// The host window, for hosts created by [`attach_to_hwnd`]. Position
    /// and size it like any child window; the scene is clipped to it.
    pub fn hwnd(&self) -> Option<HWND> {
        self.window.as_ref().map(|(hwnd, _)| *hwnd)
    }

    pub(crate) fn backdrop(&self) -> Option<&SpriteVisual> {
        self.backdrop.as_ref()
    }

    pub(crate) fn context(&self) -> &Context {
        &self.ctx
    }
}

impl Drop for SceneHost {
    fn drop(&mut self) {
        if let Some((hwnd, target)) = self.window.take() {
            let _ = target.SetRoot(None::<&windows::UI::Composition::Visual>);
            drop(target);
            // SAFETY: the window was created by this host.
            let _ = unsafe { DestroyWindow(hwnd) };
        } else if let Ok(parent) = self.visual.Parent() {
            let _ = parent.Children().and_then(|c| c.Remove(&self.visual));
        }
    }
}

/// The compositor every scene on this thread uses. Visuals passed to
/// [`attach_to_visual`] must come from it.
pub fn compositor() -> Compositor {
    Context::get().compositor.clone()
}

/// Builds the host visual and installs the backend.
fn install(scene: &Scene, ctx: &Rc<Context>, with_backdrop: bool) -> Result<(ContainerVisual, Option<SpriteVisual>)> {
    let comp = &ctx.compositor;
    let visual = comp.CreateContainerVisual()?;
    visual.SetRelativeSizeAdjustment(vec2(1.0, 1.0))?;
    visual.SetClip(&comp.CreateInsetClip()?)?;
    let backdrop = if with_backdrop {
        let sprite = comp.CreateSpriteVisual()?;
        sprite.SetRelativeSizeAdjustment(vec2(1.0, 1.0))?;
        sprite.SetBrush(&comp.CreateColorBrushWithColor(windows::UI::Color { A: 255, R: 0, G: 0, B: 0 })?)?;
        visual.Children()?.InsertAtBottom(&sprite)?;
        Some(sprite)
    } else {
        None
    };
    let scaled = comp.CreateContainerVisual()?;
    // Anti-alias the edges of transformed visuals; children inherit it.
    scaled.SetBorderMode(CompositionBorderMode::Soft)?;
    visual.Children()?.InsertAtTop(&scaled)?;
    scene.set_backend(Box::new(WinCompBackend::new(ctx.clone(), scaled, scene.downgrade())));
    Ok((visual, backdrop))
}

/// Presents `scene` in a new child window that fills `parent`'s client
/// area. The scene follows the window's DPI. The window is transparent to
/// input (`HTTRANSPARENT`); resize it from the parent's `WM_SIZE`.
///
/// # Panics
/// If the scene already has a backend.
pub fn attach_to_hwnd(scene: &Scene, parent: HWND) -> Result<SceneHost> {
    let ctx = Context::get();
    let mut rc = RECT::default();
    // SAFETY: `parent` is a window handle provided by the caller.
    unsafe { GetClientRect(parent, &mut rc)? };
    let hwnd = window::create(
        scene.downgrade(),
        true,
        WS_EX_NOREDIRECTIONBITMAP,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        (0, 0, rc.right - rc.left, rc.bottom - rc.top),
        Some(parent),
    )?;
    let interop: ICompositorDesktopInterop = ctx.compositor.cast()?;
    // SAFETY: `hwnd` is our window, on this thread.
    let target = unsafe { interop.CreateDesktopWindowTarget(hwnd, false)? };
    let (visual, backdrop) = install(scene, &ctx, false)?;
    target.SetRoot(&visual)?;
    scene.set_backing_scale(window::scale_of(hwnd));
    Ok(SceneHost { visual, backdrop, window: Some((hwnd, target)), ctx })
}

/// Presents `scene` in `container`, which must belong to [`compositor`],
/// through a new host visual that fills it. Rasterizes at scale 1; call
/// [`Scene::set_backing_scale`] to match the container's DPI.
///
/// # Panics
/// If the scene already has a backend.
pub fn attach_to_visual(scene: &Scene, container: &ContainerVisual) -> Result<SceneHost> {
    let ctx = Context::get();
    let (visual, backdrop) = install(scene, &ctx, false)?;
    container.Children()?.InsertAtTop(&visual)?;
    Ok(SceneHost { visual, backdrop, window: None, ctx })
}

/// A visible, unactivated popup window far outside the desktop, `width ×
/// height` pixels: composition renders it, and `PrintWindow` reads it back.
pub(crate) fn attach_offscreen(scene: &Scene, width: u32, height: u32, scale: f64) -> Result<SceneHost> {
    let ctx = Context::get();
    // Pixel-exact regardless of the process's DPI awareness.
    // SAFETY: restored right after creating the window.
    let previous = unsafe { SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) };
    let hwnd = window::create(
        scene.downgrade(),
        false,
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        WS_POPUP | WS_VISIBLE,
        (-20000, -20000, width as i32, height as i32),
        None,
    );
    // SAFETY: restores the previous awareness.
    unsafe { SetThreadDpiAwarenessContext(previous) };
    let hwnd = hwnd?;
    let interop: ICompositorDesktopInterop = ctx.compositor.cast()?;
    // SAFETY: `hwnd` is our window, on this thread.
    let target = unsafe { interop.CreateDesktopWindowTarget(hwnd, false)? };
    let (visual, backdrop) = install(scene, &ctx, true)?;
    target.SetRoot(&visual)?;
    scene.set_backing_scale(scale);
    Ok(SceneHost { visual, backdrop, window: Some((hwnd, target)), ctx })
}
