//! Per-thread composition context and commit scheduling.
//!
//! Windows.UI.Composition objects belong to the thread whose
//! `DispatcherQueue` created their `Compositor`. Every scene on a thread
//! shares one [`Context`]: the compositor, the Direct3D / Direct2D devices,
//! and the `CompositionGraphicsDevice` that drawing surfaces come from.

use std::cell::{Cell, RefCell};
use std::rc::Rc;
use std::time::{Duration, Instant};

use graphite::WeakScene;
use windows::Graphics::DirectX::{DirectXAlphaMode, DirectXPixelFormat};
use windows::System::{DispatcherQueue, DispatcherQueueController, DispatcherQueueHandler};
use windows::UI::Composition::{CompositionDrawingSurface, CompositionGraphicsDevice, Compositor};
use windows::Win32::Foundation::{HMODULE, RECT};
use windows::Win32::Graphics::Direct2D::Common::{D2D1_COLOR_F, D2D1_FILL_MODE_ALTERNATE, D2D1_FILL_MODE_WINDING};
use windows::Win32::Graphics::Direct2D::{
    D2D1_FACTORY_TYPE_SINGLE_THREADED, D2D1CreateFactory, ID2D1Device, ID2D1DeviceContext, ID2D1Factory1,
};
use windows::Win32::Graphics::Direct3D::{D3D_DRIVER_TYPE, D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP};
use windows::Win32::Graphics::Direct3D11::{D3D11_CREATE_DEVICE_BGRA_SUPPORT, D3D11_SDK_VERSION, D3D11CreateDevice};
use windows::Win32::Graphics::Dxgi::IDXGIDevice;
use windows::Win32::System::LibraryLoader::{GET_MODULE_HANDLE_EX_FLAG_PIN, GetModuleHandleExW, LoadLibraryW};
use windows::Win32::System::WinRT::Composition::{ICompositionDrawingSurfaceInterop, ICompositorInterop};
use windows::Win32::System::WinRT::{
    CreateDispatcherQueueController, DQTAT_COM_NONE, DQTAT_COM_STA, DQTYPE_THREAD_CURRENT, DispatcherQueueOptions,
};
use windows::Win32::UI::WindowsAndMessaging::{DispatchMessageW, MSG, PM_REMOVE, PeekMessageW, TranslateMessage};
use windows::core::{Interface, Result, w};
use windows_future::AsyncStatus;

use graphite::FillRule;

/// Shared composition and Direct2D objects of one thread.
pub(crate) struct Context {
    pub compositor: Compositor,
    pub graphics: CompositionGraphicsDevice,
    pub d2d_factory: ID2D1Factory1,
    _d2d_device: ID2D1Device,
    _controller: Option<DispatcherQueueController>,
    queue: DispatcherQueue,
}

thread_local! {
    static CONTEXT: RefCell<Option<Rc<Context>>> = const { RefCell::new(None) };
    /// Scenes waiting for their commit on this thread.
    static PENDING: RefCell<Vec<WeakScene>> = const { RefCell::new(Vec::new()) };
    static SCHEDULED: Cell<bool> = const { Cell::new(false) };
}

impl Context {
    /// This thread's context, created on first use.
    ///
    /// # Panics
    /// If composition cannot be initialized on this thread.
    pub(crate) fn get() -> Rc<Context> {
        CONTEXT.with(|cell| {
            cell.borrow_mut()
                .get_or_insert_with(|| {
                    let ctx = Rc::new(Context::create().unwrap_or_else(|err| {
                        panic!("graphite-windows: cannot initialize Windows.UI.Composition: {err}")
                    }));
                    // Never tear composition down from a thread-local
                    // destructor: at process exit that runs after the
                    // system has started unloading DLLs. The context lives
                    // as long as its thread.
                    std::mem::forget(ctx.clone());
                    ctx
                })
                .clone()
        })
    }

    fn create() -> Result<Context> {
        pin_dcomp();
        let (controller, queue) = match DispatcherQueue::GetForCurrentThread() {
            Ok(queue) => (None, queue),
            Err(_) => {
                let options = |apartment| DispatcherQueueOptions {
                    dwSize: size_of::<DispatcherQueueOptions>() as u32,
                    threadType: DQTYPE_THREAD_CURRENT,
                    apartmentType: apartment,
                };
                // An STA unless the thread already joined the MTA.
                // SAFETY: plain FFI calls with valid options.
                let controller = unsafe { CreateDispatcherQueueController(options(DQTAT_COM_STA)) }
                    .or_else(|_| unsafe { CreateDispatcherQueueController(options(DQTAT_COM_NONE)) })?;
                let queue = controller.DispatcherQueue()?;
                (Some(controller), queue)
            }
        };
        let compositor = Compositor::new()?;
        let dxgi = create_d3d_device()?;
        // SAFETY: FFI calls with valid arguments.
        let (d2d_factory, d2d_device) = unsafe {
            let factory: ID2D1Factory1 = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, None)?;
            let device = factory.CreateDevice(&dxgi)?;
            (factory, device)
        };
        let interop: ICompositorInterop = compositor.cast()?;
        // SAFETY: the Direct2D device is valid.
        let graphics = unsafe { interop.CreateGraphicsDevice(&d2d_device)? };
        Ok(Context { compositor, graphics, d2d_factory, _d2d_device: d2d_device, _controller: controller, queue })
    }

    /// A premultiplied BGRA drawing surface of `width × height` pixels.
    pub(crate) fn surface(&self, width: u32, height: u32) -> Result<CompositionDrawingSurface> {
        self.graphics.CreateDrawingSurface(
            windows::Foundation::Size { Width: width as f32, Height: height as f32 },
            DirectXPixelFormat::B8G8R8A8UIntNormalized,
            DirectXAlphaMode::Premultiplied,
        )
    }

    /// Redraws the whole of `surface` with `draw`, which gets a cleared
    /// device context and the pixel offset of the surface in it.
    pub(crate) fn draw_surface(
        &self,
        surface: &CompositionDrawingSurface,
        draw: impl FnOnce(&ID2D1DeviceContext, (f64, f64)) -> Result<()>,
    ) -> Result<()> {
        let interop: ICompositionDrawingSurfaceInterop = surface.cast()?;
        let mut offset = windows::Win32::Foundation::POINT::default();
        // SAFETY: BeginDraw/EndDraw are paired; the context is only used in between.
        unsafe {
            let dc: ID2D1DeviceContext = interop.BeginDraw(None::<*const RECT>, &mut offset)?;
            dc.Clear(Some(&D2D1_COLOR_F { r: 0.0, g: 0.0, b: 0.0, a: 0.0 }));
            let result = draw(&dc, (offset.x as f64, offset.y as f64));
            interop.EndDraw()?;
            result
        }
    }

    pub(crate) fn fill_mode(rule: FillRule) -> windows::Win32::Graphics::Direct2D::Common::D2D1_FILL_MODE {
        match rule {
            FillRule::NonZero => D2D1_FILL_MODE_WINDING,
            FillRule::EvenOdd => D2D1_FILL_MODE_ALTERNATE,
        }
    }

    /// Waits until the compositor has handed every change made so far to
    /// the system compositor, pumping this thread's messages meanwhile.
    pub(crate) fn wait_for_commit(&self) -> Result<()> {
        let op = self.compositor.RequestCommitAsync()?;
        let deadline = Instant::now() + Duration::from_secs(5);
        while op.Status()? == AsyncStatus::Started && Instant::now() < deadline {
            pump_messages();
            std::thread::sleep(Duration::from_millis(1));
        }
        Ok(())
    }
}

/// Dispatches every queued message of this thread.
pub(crate) fn pump_messages() {
    let mut msg = MSG::default();
    // SAFETY: standard message loop calls.
    unsafe {
        while PeekMessageW(&mut msg, None, 0, 0, PM_REMOVE).as_bool() {
            let _ = TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

/// Destroying a window that hosted a `DesktopWindowTarget` can unload
/// dcomp.dll while composition objects are still alive (observed on
/// Windows 11 build 26300); their next release then jumps into unmapped
/// code. Keep the module loaded for the life of the process.
fn pin_dcomp() {
    // SAFETY: loading and pinning a system DLL.
    unsafe {
        if LoadLibraryW(w!("dcomp.dll")).is_ok() {
            let mut module = HMODULE::default();
            let _ = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, w!("dcomp.dll"), &mut module);
        }
    }
}

fn create_d3d_device() -> Result<IDXGIDevice> {
    let create = |driver: D3D_DRIVER_TYPE| {
        let mut device = None;
        // SAFETY: valid out pointer; BGRA support is what Direct2D needs.
        unsafe {
            D3D11CreateDevice(
                None,
                driver,
                HMODULE::default(),
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                None,
                D3D11_SDK_VERSION,
                Some(&mut device),
                None,
                None,
            )
        }
        .map(|()| device)
    };
    let device = create(D3D_DRIVER_TYPE_HARDWARE).or_else(|_| create(D3D_DRIVER_TYPE_WARP))?;
    device.expect("D3D11CreateDevice returned no device").cast()
}

/// Commits `scene` from this thread's `DispatcherQueue`, after the current
/// message has been handled.
pub(crate) fn schedule_commit(scene: WeakScene) {
    PENDING.with(|p| p.borrow_mut().push(scene));
    if SCHEDULED.replace(true) {
        return;
    }
    let queue = Context::get().queue.clone();
    // The handler must be `Send`, so it captures nothing: the scenes stay
    // in this thread's PENDING list.
    let handler = DispatcherQueueHandler::new(|| {
        flush_pending();
        Ok(())
    });
    if !queue.TryEnqueue(&handler).unwrap_or(false) {
        SCHEDULED.set(false);
    }
}

fn flush_pending() {
    SCHEDULED.set(false);
    let scenes = PENDING.with(|p| std::mem::take(&mut *p.borrow_mut()));
    for scene in scenes {
        if let Some(scene) = scene.upgrade() {
            scene.commit();
        }
    }
}
