//! Windows shell: a top-level window hosting the scene through
//! graphite-windows (a child window with a composition target), and a
//! high-resolution waitable timer driving the animation from the message
//! loop. graphite-windows commits from the thread's `DispatcherQueue` after
//! each tick.
//!
//! `SetTimer` is not used: its period is rounded up to the system timer
//! resolution, so a 16 ms timer ticks at about 39 Hz.

use std::cell::RefCell;
use std::process::ExitCode;
use std::time::{Duration, Instant};

use graphite::Scene;
use windows::Win32::Foundation::{HANDLE, HWND, LPARAM, LRESULT, RECT, WAIT_OBJECT_0, WPARAM};
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::Win32::System::Threading::{
    CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, CreateWaitableTimerExW, INFINITE, SetWaitableTimer, TIMER_ALL_ACCESS,
};
use windows::Win32::UI::HiDpi::{
    AdjustWindowRectExForDpi, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2, GetDpiForSystem,
    SetProcessDpiAwarenessContext,
};
use windows::Win32::UI::WindowsAndMessaging::{
    CW_USEDEFAULT, CreateWindowExW, DefWindowProcW, DispatchMessageW, IDC_ARROW, LoadCursorW, MSG, MWMO_INPUTAVAILABLE,
    MoveWindow, MsgWaitForMultipleObjectsEx, PM_REMOVE, PeekMessageW, PostQuitMessage, QS_ALLINPUT, RegisterClassW,
    SWP_NOACTIVATE, SWP_NOZORDER, SetWindowPos, TranslateMessage, WINDOW_EX_STYLE, WM_DESTROY, WM_DPICHANGED, WM_QUIT,
    WM_SIZE, WNDCLASSW, WS_OVERLAPPEDWINDOW, WS_VISIBLE,
};
use windows::core::{PCWSTR, w};

use crate::{App, Control, Rgba};

struct Shell {
    app: App,
    host: Option<graphite_windows::SceneHost>,
    exit_code: i32,
}

thread_local! {
    static SHELL: RefCell<Option<Shell>> = const { RefCell::new(None) };
}

fn dpi_aware() {
    // SAFETY: a process-wide setting, made before any window exists.
    let _ = unsafe { SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) };
}

pub fn run(app: App) -> ExitCode {
    dpi_aware();
    let size = app.scene().root().bounds().size;
    // SAFETY: standard Win32 window creation and message loop on this thread.
    unsafe {
        let instance = GetModuleHandleW(None).expect("module handle");
        RegisterClassW(&WNDCLASSW {
            lpfnWndProc: Some(window_proc),
            hInstance: instance.into(),
            lpszClassName: w!("GraphiteDemo"),
            hCursor: LoadCursorW(None, IDC_ARROW).unwrap_or_default(),
            ..Default::default()
        });
        // Size the client area to the scene at the system DPI; WM_DPICHANGED
        // adjusts it on other monitors.
        let dpi = GetDpiForSystem();
        let scale = dpi as f64 / 96.0;
        let mut rect = RECT {
            left: 0,
            top: 0,
            right: (size.width * scale).round() as i32,
            bottom: (size.height * scale).round() as i32,
        };
        let _ = AdjustWindowRectExForDpi(&mut rect, WS_OVERLAPPEDWINDOW, false, WINDOW_EX_STYLE(0), dpi);
        SHELL.with(|s| *s.borrow_mut() = Some(Shell { app, host: None, exit_code: 0 }));
        let hwnd = CreateWindowExW(
            WINDOW_EX_STYLE(0),
            w!("GraphiteDemo"),
            w!("graphite demo (Windows.UI.Composition)"),
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            rect.right - rect.left,
            rect.bottom - rect.top,
            None,
            None,
            Some(instance.into()),
            None,
        )
        .expect("cannot create the demo window");
        SHELL.with(|s| {
            let mut s = s.borrow_mut();
            let shell = s.as_mut().unwrap();
            shell.host =
                Some(graphite_windows::attach_to_hwnd(shell.app.scene(), hwnd).expect("cannot attach the scene"));
        });
        run_loop();
    }
    let code = SHELL.with(|s| s.borrow_mut().take().map_or(0, |shell| shell.exit_code));
    ExitCode::from(code as u8)
}

/// Ticks the app at `FPS` (on absolute deadlines, skipping frames when
/// late) while dispatching messages, until `WM_QUIT`.
fn run_loop() {
    let frame_time = Duration::from_secs_f64(1.0 / crate::FPS);
    // SAFETY: a timer handle owned by this function, used on this thread.
    unsafe {
        let timer =
            CreateWaitableTimerExW(None, PCWSTR::null(), CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS.0)
                .expect("cannot create a high-resolution timer");
        let start = Instant::now();
        let mut frame: u32 = 0;
        let arm = |timer: HANDLE, frame: u32| {
            let due = start + frame_time * frame;
            // Negative: relative, in 100 ns units.
            let relative = -((due.saturating_duration_since(Instant::now()).as_nanos() / 100).max(1) as i64);
            let _ = SetWaitableTimer(timer, &relative, 0, None, None, false);
        };
        arm(timer, 1);
        loop {
            let woke = MsgWaitForMultipleObjectsEx(Some(&[timer]), INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if woke == WAIT_OBJECT_0 {
                tick();
                let due = (start.elapsed().as_secs_f64() / frame_time.as_secs_f64()) as u32;
                frame = (frame + 1).max(due);
                arm(timer, frame + 1);
            }
            let mut msg = MSG::default();
            while PeekMessageW(&mut msg, None, 0, 0, PM_REMOVE).as_bool() {
                if msg.message == WM_QUIT {
                    return;
                }
                let _ = TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }
}

fn tick() {
    let control = SHELL.with(|s| s.borrow_mut().as_mut().map(|shell| shell.app.tick()));
    if let Some(Control::Exit(code)) = control {
        SHELL.with(|s| {
            if let Some(shell) = s.borrow_mut().as_mut() {
                shell.exit_code = code;
            }
        });
        // SAFETY: posts to this thread's queue.
        unsafe { PostQuitMessage(code) };
    }
}

unsafe extern "system" fn window_proc(hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    // SAFETY: called by the system with valid arguments for `hwnd`.
    unsafe {
        match msg {
            WM_SIZE => {
                let (w, h) = ((lparam.0 & 0xffff) as i32, ((lparam.0 >> 16) & 0xffff) as i32);
                SHELL.with(|s| {
                    if let Some(child) = s.borrow().as_ref().and_then(|shell| shell.host.as_ref()?.hwnd()) {
                        let _ = MoveWindow(child, 0, 0, w, h, true);
                    }
                });
                LRESULT(0)
            }
            WM_DPICHANGED => {
                // Use the size Windows suggests for the new DPI.
                let r = &*(lparam.0 as *const RECT);
                let _ = SetWindowPos(
                    hwnd,
                    None,
                    r.left,
                    r.top,
                    r.right - r.left,
                    r.bottom - r.top,
                    SWP_NOZORDER | SWP_NOACTIVATE,
                );
                LRESULT(0)
            }
            WM_DESTROY => {
                PostQuitMessage(0);
                LRESULT(0)
            }
            _ => DefWindowProcW(hwnd, msg, wparam, lparam),
        }
    }
}

pub fn snapshot(scene: &Scene) -> Option<Rgba> {
    dpi_aware();
    let px = graphite_windows::offscreen::render(scene, 1.0).ok()?;
    Some(Rgba { width: px.width, height: px.height, data: px.data })
}
