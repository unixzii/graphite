#include "internal.h"

bool frame_at(const frame *f, gr_point p, rgba8 *out)
{
    double x = floor(p.x * f->scale), y = floor(p.y * f->scale);
    if (!isfinite(x) || !isfinite(y) || x < 0 || y < 0 || x >= f->pixels.width || y >= f->pixels.height)
        return false;
    memcpy(out->v, f->pixels.data + ((size_t)y * f->pixels.width + (size_t)x) * 4, 4);
    return true;
}

profile frame_profile(const frame *f, gr_point from, gr_point to)
{
    double dx = to.x - from.x, dy = to.y - from.y, len = sqrt(dx * dx + dy * dy);
    profile result = {0};
    if (!(len > 0))
        return result;
    size_t steps = (size_t)floor(len * f->scale);
    result.data = calloc(steps + 1, sizeof(sample));
    if (!result.data)
        abort();
    for (size_t i = 0; i <= steps; ++i)
    {
        double d = ((double)i + 0.5) / f->scale;
        if (d > len)
            continue;
        rgba8 pixel;
        if (!frame_at(f, POINT(from.x + dx * d / len, from.y + dy * d / len), &pixel))
            continue;
        sample *s = &result.data[result.count++];
        s->distance = d;
        for (size_t channel = 0; channel < 4; ++channel)
            s->channels[channel] = pixel.v[channel] / 255.0;
    }
    return result;
}

double profile_crossing(profile p, size_t channel, double threshold)
{
    for (size_t i = 1; i < p.count; ++i)
    {
        sample a = p.data[i - 1], b = p.data[i];
        double x = a.channels[channel], y = b.channels[channel];
        if ((x - threshold) * (y - threshold) <= 0 && x != y)
            return a.distance + (threshold - x) / (y - x) * (b.distance - a.distance);
    }
    return NAN;
}

double profile_coverage(profile p, size_t channel, double scale)
{
    double sum = 0;
    for (size_t i = 0; i < p.count; ++i)
        sum += p.data[i].channels[channel];
    return sum / scale;
}
