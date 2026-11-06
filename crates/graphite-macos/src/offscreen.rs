//! Window-less rendering of a Core Animation layer tree with `CARenderer`.
//!
//! Renders exactly what the window server would (same compositor), which
//! makes it suitable for golden-image tests.

use std::ptr::{self, NonNull};

use objc2::rc::Retained;
use objc2::runtime::AnyObject;
use objc2_core_foundation::{CGPoint, CGRect, CGSize};
use objc2_foundation::{NSDictionary, NSString};
use objc2_metal::{
    MTLCommandBuffer, MTLCommandQueue, MTLCreateSystemDefaultDevice, MTLDevice, MTLOrigin, MTLPixelFormat, MTLRegion,
    MTLSize, MTLStorageMode, MTLTexture, MTLTextureDescriptor, MTLTextureUsage,
};
use objc2_quartz_core::{
    CACurrentMediaTime, CALayer, CARenderer, CATransaction, CATransform3D, kCARendererColorSpace,
    kCARendererMetalCommandQueue,
};

use crate::convert::{cf_as_object, srgb};

/// Premultiplied RGBA8 pixels, row 0 at the top.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Pixels {
    pub width: usize,
    pub height: usize,
    pub data: Vec<u8>,
}

impl Pixels {
    /// The `[r, g, b, a]` value at `(x, y)`, with `(0, 0)` the top-left pixel.
    pub fn at(&self, x: usize, y: usize) -> [u8; 4] {
        let i = (y * self.width + x) * 4;
        [self.data[i], self.data[i + 1], self.data[i + 2], self.data[i + 3]]
    }
}

/// Renders `layer` (which must not have a superlayer) at `scale` device
/// pixels per point. `width` × `height` is the area to render, in points,
/// starting at the layer's superlayer-space origin; the result is
/// `round(width · scale)` × `round(height · scale)` pixels. Set the layer
/// tree's `contentsScale` to `scale` for crisp contents.
pub fn render_scaled(layer: &CALayer, width: f64, height: f64, scale: f64) -> Option<Pixels> {
    let (w, h) = ((width * scale).round() as usize, (height * scale).round() as usize);
    CATransaction::begin();
    CATransaction::setDisableActions(true);
    let wrapper = CALayer::new();
    wrapper.setAnchorPoint(CGPoint::new(0.0, 0.0));
    wrapper.setPosition(CGPoint::new(0.0, 0.0));
    wrapper.setBounds(CGRect::new(CGPoint::new(0.0, 0.0), CGSize::new(w as f64, h as f64)));
    // Applied around the wrapper's anchor point, its (unflipped) bottom-left.
    wrapper.setSublayerTransform(CATransform3D::new_scale(scale, scale, 1.0));
    wrapper.addSublayer(layer);
    CATransaction::commit();

    let pixels = render(&wrapper, w, h);

    CATransaction::begin();
    CATransaction::setDisableActions(true);
    layer.removeFromSuperlayer();
    CATransaction::commit();
    pixels
}

/// Renders `layer` (which must not have a superlayer) into a `width` ×
/// `height` pixel image; one point maps to one pixel. Returns `None` if no
/// Metal device is available.
pub fn render(layer: &CALayer, width: usize, height: usize) -> Option<Pixels> {
    let device = MTLCreateSystemDefaultDevice()?;
    // SAFETY: valid pixel format and non-zero size.
    let desc = unsafe {
        MTLTextureDescriptor::texture2DDescriptorWithPixelFormat_width_height_mipmapped(
            MTLPixelFormat::BGRA8Unorm,
            width,
            height,
            false,
        )
    };
    desc.setUsage(MTLTextureUsage::RenderTarget | MTLTextureUsage::ShaderRead | MTLTextureUsage::ShaderWrite);
    desc.setStorageMode(MTLStorageMode::Shared);
    let texture = device.newTextureWithDescriptor(&desc)?;
    let queue = device.newCommandQueue()?;

    let color_space = srgb();
    // SAFETY: static CFString keys bridged to NSString.
    let keys: [&NSString; 2] = unsafe { [kCARendererMetalCommandQueue, kCARendererColorSpace] };
    let queue_obj: &AnyObject = as_object(&queue);
    let values: [&AnyObject; 2] = [queue_obj, cf_as_object(&*color_space)];
    let options = NSDictionary::from_slices(&keys, &values);
    // SAFETY: erasing the key type of a dictionary is always valid.
    let options: Retained<NSDictionary> = unsafe { Retained::cast_unchecked(options) };

    // SAFETY: the texture outlives the renderer, and the options dictionary
    // has the documented value types.
    let renderer = unsafe { CARenderer::rendererWithMTLTexture_options(&texture, Some(&options)) };
    renderer.setLayer(Some(layer));
    let bounds = CGRect::new(CGPoint::new(0.0, 0.0), CGSize::new(width as f64, height as f64));
    renderer.setBounds(bounds);
    // Attaching the layer to the renderer's context only takes effect once
    // committed; flush so the renderer sees the current model tree.
    CATransaction::flush();
    // SAFETY: a null timestamp is allowed.
    unsafe { renderer.beginFrameAtTime_timeStamp(CACurrentMediaTime(), ptr::null_mut()) };
    renderer.addUpdateRect(bounds);
    renderer.render();
    renderer.endFrame();
    renderer.setLayer(None);

    // The renderer encodes into `queue`; an empty buffer committed after it
    // completes only once the frame has.
    let fence = queue.commandBuffer()?;
    fence.commit();
    fence.waitUntilCompleted();

    let mut bgra = vec![0u8; width * height * 4];
    let region = MTLRegion { origin: MTLOrigin { x: 0, y: 0, z: 0 }, size: MTLSize { width, height, depth: 1 } };
    // SAFETY: `bgra` holds exactly `height` rows of `width * 4` bytes.
    unsafe {
        texture.getBytes_bytesPerRow_fromRegion_mipmapLevel(
            NonNull::new(bgra.as_mut_ptr().cast()).unwrap(),
            width * 4,
            region,
            0,
        );
    }
    // The texture's first row is the bottom of the screen (Core Animation's
    // y = 0 in an unflipped tree). Flip to top-down and swizzle to RGBA.
    let row = width * 4;
    let mut rgba = Vec::with_capacity(bgra.len());
    for src in bgra.chunks_exact(row).rev() {
        for px in src.as_chunks::<4>().0 {
            rgba.extend_from_slice(&[px[2], px[1], px[0], px[3]]);
        }
    }
    Some(Pixels { width, height, data: rgba })
}

fn as_object<T: ?Sized + objc2::Message>(obj: &Retained<T>) -> &AnyObject {
    // SAFETY: every Objective-C object can be viewed as `AnyObject`.
    unsafe { &*(Retained::as_ptr(obj) as *const AnyObject) }
}
