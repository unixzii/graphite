//! `attach_to_hwnd` in a real window: commit scheduling through the
//! thread's `DispatcherQueue`, DPI tracking, filling the parent, and what
//! the window actually shows. Composition objects are bound to this thread,
//! so the target has its own `main` (`harness = false`); it needs an
//! interactive desktop session.

#[cfg(windows)]
fn main() -> std::process::ExitCode {
    use std::time::{Duration, Instant};

    use graphite::*;
    use graphite_windows::offscreen::{Pixels, capture_window};
    use graphite_windows::windows::Win32::Foundation::{HWND, LPARAM, LRESULT, RECT, WPARAM};
    use graphite_windows::windows::Win32::System::LibraryLoader::GetModuleHandleW;
    use graphite_windows::windows::Win32::UI::HiDpi::{
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2, GetDpiForWindow, SetProcessDpiAwarenessContext,
    };
    use graphite_windows::windows::Win32::UI::WindowsAndMessaging::{
        CreateWindowExW, DefWindowProcW, DestroyWindow, DispatchMessageW, GetClientRect, MSG, PM_REMOVE, PeekMessageW,
        RegisterClassW, TranslateMessage, WINDOW_EX_STYLE, WNDCLASSW, WS_POPUP, WS_VISIBLE,
    };
    use graphite_windows::windows::core::w;

    unsafe extern "system" fn proc(h: HWND, m: u32, w: WPARAM, l: LPARAM) -> LRESULT {
        // SAFETY: forwarded unchanged.
        unsafe { DefWindowProcW(h, m, w, l) }
    }

    /// Pumps messages (the DispatcherQueue runs from them) until `done`.
    fn spin_until(what: &str, mut done: impl FnMut() -> bool) {
        let deadline = Instant::now() + Duration::from_secs(5);
        while !done() {
            assert!(Instant::now() < deadline, "timed out waiting for {what}");
            let mut msg = MSG::default();
            // SAFETY: a standard message pump.
            unsafe {
                while PeekMessageW(&mut msg, None, 0, 0, PM_REMOVE).as_bool() {
                    let _ = TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
            std::thread::sleep(Duration::from_millis(5));
        }
    }

    // SAFETY: process-wide setting made before any window exists.
    let _ = unsafe { SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) };

    // A visible top-level window, in a corner of the primary monitor.
    // SAFETY: standard window creation.
    let parent = unsafe {
        let instance = GetModuleHandleW(None).unwrap();
        RegisterClassW(&WNDCLASSW {
            lpfnWndProc: Some(proc),
            hInstance: instance.into(),
            lpszClassName: w!("GraphiteWindowTest"),
            ..Default::default()
        });
        CreateWindowExW(
            WINDOW_EX_STYLE(0),
            w!("GraphiteWindowTest"),
            w!("graphite window test"),
            WS_POPUP | WS_VISIBLE,
            40,
            40,
            300,
            200,
            None,
            None,
            Some(instance.into()),
            None,
        )
        .unwrap()
    };

    let scene = Scene::new(Size::new(120.0, 80.0));
    let square = Layer::new(&scene);
    square.set_frame(Rect::new(0.0, 0.0, 10.0, 10.0));
    square.set_background(Some(Color::rgb(1.0, 0.0, 0.0)));
    scene.root().add_sublayer(&square);

    let host = graphite_windows::attach_to_hwnd(&scene, parent).unwrap();
    let child = host.hwnd().unwrap();
    let mut client = RECT::default();
    // SAFETY: our window.
    unsafe { GetClientRect(child, &mut client).unwrap() };
    assert_eq!((client.right, client.bottom), (300, 200), "the host fills its parent");

    // SAFETY: our window.
    let dpi_scale = unsafe { GetDpiForWindow(child) } as f64 / 96.0;
    assert_eq!(scene.backing_scale(), dpi_scale, "the scene follows the window's DPI");

    // The DispatcherQueue commits without any explicit Scene::commit.
    spin_until("the first commit", || !scene.needs_commit());
    let px = |p: &Pixels, x: f64, y: f64| p.at((x * dpi_scale) as u32, (y * dpi_scale) as u32);
    let frame = capture_window(child).unwrap();
    assert_eq!(px(&frame, 5.0, 5.0), [255, 0, 0, 255], "the layer at the top-left, scaled by the DPI");
    assert_eq!(px(&frame, 5.0, 15.0)[..3], [0, 0, 0], "nothing below it");

    // Later changes are committed too, once the message loop has gone idle.
    let settle = Instant::now() + Duration::from_millis(500);
    spin_until("the window to settle", || Instant::now() > settle);
    square.set_position(Point::new(65.0, 45.0));
    spin_until("the second commit", || !scene.needs_commit());
    let frame = capture_window(child).unwrap();
    assert_eq!(px(&frame, 65.0, 45.0), [255, 0, 0, 255], "the moved layer");
    assert_eq!(px(&frame, 5.0, 5.0)[..3], [0, 0, 0], "its old position");

    drop(host);
    // SAFETY: our window.
    unsafe { DestroyWindow(parent).unwrap() };
    println!("window: ok (DPI scale {dpi_scale})");
    std::process::ExitCode::SUCCESS
}

#[cfg(not(windows))]
fn main() {}
