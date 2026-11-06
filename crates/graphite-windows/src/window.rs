//! The window class that hosts a scene's `DesktopWindowTarget`.

use std::sync::Once;

use graphite::WeakScene;
use windows::Win32::Foundation::{HWND, LPARAM, LRESULT, WPARAM};
use windows::Win32::Graphics::Gdi::ValidateRect;
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::Win32::UI::HiDpi::GetDpiForWindow;
use windows::Win32::UI::WindowsAndMessaging::{
    CREATESTRUCTW, CS_HREDRAW, CS_VREDRAW, CreateWindowExW, DefWindowProcW, GWLP_USERDATA, GetWindowLongPtrW,
    HTTRANSPARENT, RegisterClassW, SetWindowLongPtrW, WINDOW_EX_STYLE, WINDOW_STYLE, WM_DPICHANGED,
    WM_DPICHANGED_AFTERPARENT, WM_ERASEBKGND, WM_NCCREATE, WM_NCDESTROY, WM_NCHITTEST, WM_PAINT, WNDCLASSW,
};
use windows::core::{PCWSTR, Result, w};

const CLASS: PCWSTR = w!("GraphiteSceneHost");

/// Per-window state, owned through `GWLP_USERDATA`.
struct WindowData {
    scene: WeakScene,
    /// Whether the window follows its DPI (child hosts) or keeps the scale
    /// it was given (offscreen hosts).
    track_dpi: bool,
}

fn register_class() {
    static ONCE: Once = Once::new();
    ONCE.call_once(|| {
        // SAFETY: registering a class with a static name and a valid procedure.
        unsafe {
            let instance = GetModuleHandleW(None).unwrap_or_default();
            RegisterClassW(&WNDCLASSW {
                style: CS_HREDRAW | CS_VREDRAW,
                lpfnWndProc: Some(window_proc),
                hInstance: instance.into(),
                lpszClassName: CLASS,
                ..Default::default()
            });
        }
    });
}

/// Creates a host window. `parent` is `None` for top-level (offscreen) windows.
pub(crate) fn create(
    scene: WeakScene,
    track_dpi: bool,
    ex_style: WINDOW_EX_STYLE,
    style: WINDOW_STYLE,
    (x, y, width, height): (i32, i32, i32, i32),
    parent: Option<HWND>,
) -> Result<HWND> {
    register_class();
    let data = Box::into_raw(Box::new(WindowData { scene, track_dpi }));
    // SAFETY: the class is registered; `data` is reclaimed in WM_NCDESTROY.
    // (It leaks if creation fails before WM_NCCREATE.)
    unsafe {
        let instance = GetModuleHandleW(None)?;
        CreateWindowExW(
            ex_style,
            CLASS,
            w!("graphite"),
            style,
            x,
            y,
            width,
            height,
            parent,
            None,
            Some(instance.into()),
            Some(data as *const _),
        )
    }
}

/// Device pixels per point for `hwnd`.
pub(crate) fn scale_of(hwnd: HWND) -> f64 {
    // SAFETY: any window handle is accepted.
    let dpi = unsafe { GetDpiForWindow(hwnd) };
    if dpi == 0 { 1.0 } else { dpi as f64 / 96.0 }
}

unsafe extern "system" fn window_proc(hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    // SAFETY: GWLP_USERDATA holds the `WindowData` installed at WM_NCCREATE.
    unsafe {
        match msg {
            WM_NCCREATE => {
                let create = &*(lparam.0 as *const CREATESTRUCTW);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, create.lpCreateParams as isize);
            }
            WM_NCDESTROY => {
                let data = SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0) as *mut WindowData;
                if !data.is_null() {
                    drop(Box::from_raw(data));
                }
            }
            // Composition draws everything; there is no GDI content.
            WM_ERASEBKGND => return LRESULT(1),
            WM_PAINT => {
                let _ = ValidateRect(Some(hwnd), None);
                return LRESULT(0);
            }
            // A rendering surface: input goes to the window underneath.
            WM_NCHITTEST => return LRESULT(HTTRANSPARENT as isize),
            WM_DPICHANGED | WM_DPICHANGED_AFTERPARENT => {
                let data = GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *const WindowData;
                if let Some(data) = data.as_ref()
                    && data.track_dpi
                    && let Some(scene) = data.scene.upgrade()
                {
                    scene.set_backing_scale(scale_of(hwnd));
                }
            }
            _ => {}
        }
        DefWindowProcW(hwnd, msg, wparam, lparam)
    }
}
