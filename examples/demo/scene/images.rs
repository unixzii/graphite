//! Procedurally generated bitmaps (the demo ships no asset files).

use graphite::Image;

fn mix(a: [f64; 3], b: [f64; 3], t: f64) -> [f64; 3] {
    [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t]
}

fn rgb(hex: u32) -> [f64; 3] {
    [(hex >> 16 & 0xFF) as f64 / 255.0, (hex >> 8 & 0xFF) as f64 / 255.0, (hex & 0xFF) as f64 / 255.0]
}

fn smoothstep(e0: f64, e1: f64, x: f64) -> f64 {
    let t = ((x - e0) / (e1 - e0)).clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}

/// A sunset landscape (`width` × `height` points at `scale`): sky gradient,
/// sun, two mountain ranges and a lake with the sun's reflection.
pub fn landscape(width: u32, height: u32, scale: f64) -> Image {
    let (w, h) = ((width as f64 * scale) as u32, (height as f64 * scale) as u32);
    let mut px = Vec::with_capacity((w * h * 4) as usize);
    let horizon = 0.62;
    let sun = (0.68, 0.44, 0.09);
    for y in 0..h {
        for x in 0..w {
            let (u, v) = (x as f64 / w as f64, y as f64 / h as f64);
            let aspect = w as f64 / h as f64;
            let sky = |v: f64| {
                let t = (v / horizon).clamp(0.0, 1.0);
                if t < 0.55 {
                    mix(rgb(0x1B1F5E), rgb(0x8E3B8A), t / 0.55)
                } else {
                    mix(rgb(0x8E3B8A), rgb(0xFF9A5A), (t - 0.55) / 0.45)
                }
            };
            let sun_at = |u: f64, v: f64| {
                let d = (((u - sun.0) * aspect).powi(2) + (v - sun.1).powi(2)).sqrt();
                (1.0 - smoothstep(sun.2 - 0.008, sun.2 + 0.008, d), (1.0 - d * 2.2).clamp(0.0, 1.0).powi(3))
            };
            let far = 0.50 + 0.05 * (u * 9.0).sin() + 0.03 * (u * 23.0 + 1.0).sin();
            let near = 0.56 + 0.04 * (u * 5.0 + 2.0).sin() + 0.02 * (u * 17.0).cos();
            let color = if v < horizon {
                let (disk, glow) = sun_at(u, v);
                let mut c = mix(sky(v), rgb(0xFFE7A3), glow * 0.6);
                c = mix(c, rgb(0xFFF4D6), disk);
                if v > far {
                    c = mix(c, rgb(0x4A2F6B), 0.92);
                }
                if v > near {
                    c = rgb(0x24183D);
                }
                c
            } else {
                // The lake mirrors the sky, darker, with a rippled sun path.
                let mirrored = 2.0 * horizon - v;
                let ripple = (v * 220.0).sin() * 0.004;
                let (disk, glow) = sun_at(u + ripple, mirrored);
                let mut c = mix(sky(mirrored), rgb(0x0E1033), 0.45 + (v - horizon) * 0.8);
                c = mix(c, rgb(0xFFB070), (glow * 0.5 + disk * 0.6) * (0.5 + 0.5 * (v * 300.0).sin().abs()));
                c
            };
            px.extend(color.iter().map(|c| (c.clamp(0.0, 1.0) * 255.0).round() as u8));
            px.push(255);
        }
    }
    Image::from_rgba8_premultiplied(w, h, scale, px)
}

/// A small 8 × 8 checkerboard, shown magnified (one point per texel).
pub fn checker(a: u32, b: u32) -> Image {
    let px: Vec<u8> = (0..64)
        .flat_map(|i| {
            let c = if (i % 8 + i / 8) % 2 == 0 { a } else { b };
            [(c >> 16) as u8, (c >> 8) as u8, c as u8, 255]
        })
        .collect();
    Image::from_rgba8_premultiplied(8, 8, 1.0, px)
}
