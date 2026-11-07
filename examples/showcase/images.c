#include "showcase.h"

typedef struct
{
    double r, g, b;
} rgb;

static rgb rgb_hex(uint32_t value)
{
    return (rgb){((value >> 16) & 255) / 255.0, ((value >> 8) & 255) / 255.0, (value & 255) / 255.0};
}

static rgb mix(rgb a, rgb b, double t)
{
    return (rgb){a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

static double clamp(double x)
{
    return fmin(1, fmax(0, x));
}

static rgb sky(double v)
{
    double t = clamp(v / 0.62);
    return t < 0.55 ? mix(rgb_hex(0x1B1F5E), rgb_hex(0x8E3B8A), t / 0.55)
                    : mix(rgb_hex(0x8E3B8A), rgb_hex(0xFF9A5A), (t - 0.55) / 0.45);
}

static void sun_at(double u, double v, double aspect, double *disk, double *glow)
{
    double x = (u - 0.68) * aspect, y = v - 0.44, dist = sqrt(x * x + y * y), g = clamp(1 - dist * 2.2);
    *disk = 1 - smooth((dist - (0.09 - 0.008)) / 0.016);
    *glow = g * g * g;
}

gr_image *landscape(uint32_t width, uint32_t height, double scale)
{
    uint32_t w = (uint32_t)(width * scale), h = (uint32_t)(height * scale);
    size_t count = (size_t)w * h * 4;
    uint8_t *px = malloc(count);
    if (!px)
        abort();
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
        {
            double u = (double)x / w, v = (double)y / h, aspect = (double)w / h;
            double far = 0.50 + 0.05 * sin(u * 9) + 0.03 * sin(u * 23 + 1);
            double near = 0.56 + 0.04 * sin(u * 5 + 2) + 0.02 * cos(u * 17);
            double disk, glow;
            rgb c;
            if (v < 0.62)
            {
                sun_at(u, v, aspect, &disk, &glow);
                c = mix(sky(v), rgb_hex(0xFFE7A3), glow * 0.6);
                c = mix(c, rgb_hex(0xFFF4D6), disk);
                if (v > far)
                    c = mix(c, rgb_hex(0x4A2F6B), 0.92);
                if (v > near)
                    c = rgb_hex(0x24183D);
            }
            else
            {
                double mirrored = 2 * 0.62 - v, ripple = sin(v * 220) * 0.004;
                sun_at(u + ripple, mirrored, aspect, &disk, &glow);
                c = mix(sky(mirrored), rgb_hex(0x0E1033), 0.45 + (v - 0.62) * 0.8);
                c = mix(c, rgb_hex(0xFFB070), (glow * 0.5 + disk * 0.6) * (0.5 + 0.5 * fabs(sin(v * 300))));
            }
            size_t i = ((size_t)y * w + x) * 4;
            px[i] = (uint8_t)round(clamp(c.r) * 255);
            px[i + 1] = (uint8_t)round(clamp(c.g) * 255);
            px[i + 2] = (uint8_t)round(clamp(c.b) * 255);
            px[i + 3] = 255;
        }
    gr_image *image = gr_image_new(w, h, scale, (gr_bytes){px, count});
    free(px);
    return image;
}

gr_image *checker(uint32_t a, uint32_t b)
{
    uint8_t px[256];
    for (unsigned i = 0; i < 64; ++i)
    {
        uint32_t c = (i % 8 + i / 8) % 2 == 0 ? a : b;
        px[i * 4] = (uint8_t)(c >> 16);
        px[i * 4 + 1] = (uint8_t)(c >> 8);
        px[i * 4 + 2] = (uint8_t)c;
        px[i * 4 + 3] = 255;
    }
    return gr_image_new(8, 8, 1, (gr_bytes){px, 256});
}
