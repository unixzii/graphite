use graphite::*;
use std::time::Instant;
#[cfg(feature = "metrics")]
mod alloc {
    use std::alloc::{GlobalAlloc, Layout, System};
    use std::sync::atomic::{AtomicUsize, Ordering::Relaxed};
    pub static LIVE: AtomicUsize = AtomicUsize::new(0);
    pub static COUNT: AtomicUsize = AtomicUsize::new(0);
    unsafe extern "C" {
        fn malloc_size(p: *const u8) -> usize;
    }
    pub struct Counter;
    unsafe impl GlobalAlloc for Counter {
        unsafe fn alloc(&self, l: Layout) -> *mut u8 {
            let p = unsafe { System.alloc(l) };
            if !p.is_null() {
                LIVE.fetch_add(unsafe { malloc_size(p) }, Relaxed);
                COUNT.fetch_add(1, Relaxed);
            }
            p
        }
        unsafe fn dealloc(&self, p: *mut u8, l: Layout) {
            LIVE.fetch_sub(unsafe { malloc_size(p) }, Relaxed);
            unsafe { System.dealloc(p, l) };
        }
        unsafe fn realloc(&self, p: *mut u8, l: Layout, n: usize) -> *mut u8 {
            let old = unsafe { malloc_size(p) };
            let q = unsafe { System.realloc(p, l, n) };
            if !q.is_null() {
                LIVE.fetch_sub(old, Relaxed);
                LIVE.fetch_add(unsafe { malloc_size(q) }, Relaxed);
                COUNT.fetch_add(1, Relaxed);
            }
            q
        }
    }
}
#[cfg(feature = "metrics")]
#[global_allocator]
static ALLOC: alloc::Counter = alloc::Counter;
fn stats() -> (usize, usize) {
    #[cfg(feature = "metrics")]
    {
        use std::sync::atomic::Ordering::Relaxed;
        return (alloc::LIVE.load(Relaxed), alloc::COUNT.load(Relaxed));
    }
    #[cfg(not(feature = "metrics"))]
    {
        (0, 0)
    }
}
struct Sink;
impl Backend for Sink {
    fn request_commit(&mut self) {}
    fn apply(&mut self, b: CommitBatch) {
        std::hint::black_box(&b);
    }
}
fn main() {
    let args: Vec<_> = std::env::args().collect();
    let n: usize = args[1].parse().unwrap();
    let changed: usize = args[2].parse().unwrap();
    let rounds: usize = args[3].parse().unwrap();
    let baseline = stats();
    let start = Instant::now();
    let scene = Scene::new(Size::new(1000., 1000.));
    scene.set_backend(Box::new(Sink));
    let root = scene.root();
    let mut layers = Vec::with_capacity(n);
    for _ in 0..n {
        let l = Layer::new(&scene);
        root.add_sublayer(&l);
        layers.push(l);
    }
    scene.commit();
    let create_ns = start.elapsed().as_nanos();
    for frame in 0..20 {
        for l in layers.iter().take(changed) {
            l.set_opacity(if frame % 2 == 0 { 0.5 } else { 1. });
        }
        scene.commit();
    }
    let before = stats();
    let start = Instant::now();
    for frame in 0..rounds {
        std::hint::black_box(&scene);
        for l in layers.iter().take(changed) {
            l.set_opacity(if frame % 2 == 0 { 0.5 } else { 1. });
        }
        scene.commit();
    }
    let update_ns = start.elapsed().as_nanos();
    let after = stats();
    drop(layers);
    drop(root);
    drop(scene);
    let end = stats();
    println!(
        "{n},{changed},{rounds},{create_ns},{update_ns},{},{},{}",
        before.0 - baseline.0,
        after.1 - before.1,
        end.0 as i64 - baseline.0 as i64
    );
}
