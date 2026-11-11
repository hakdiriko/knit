#include "Medium.h"

namespace dendrite::ui
{
void Medium::setSize (float width, float height, float cellSize)
{
    cell = cellSize;
    nx = std::max (4, (int) std::ceil (width / cell));
    ny = std::max (4, (int) std::ceil (height / cell));
    const size_t n = (size_t) ((nx + 2) * (ny + 2));
    for (auto* f : { &u, &v, &u0, &v0, &pressure, &divergence })
        f->assign (n, 0.0f);
}

void Medium::stir (juce::Point<float> p, juce::Point<float> vel, float radius, float amount)
{
    if (nx == 0)
        return;
    const float gx = p.x / cell + 0.5f, gy = p.y / cell + 0.5f, r = radius / cell;
    const int i0 = std::max (1, (int) (gx - r - 1.0f)), i1 = std::min (nx, (int) (gx + r + 1.0f));
    const int j0 = std::max (1, (int) (gy - r - 1.0f)), j1 = std::min (ny, (int) (gy + r + 1.0f));
    const float s2 = 2.0f * r * r * 0.25f;
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i)
        {
            const float dx = (float) i - gx, dy = (float) j - gy;
            const float w = amount * std::exp (-(dx * dx + dy * dy) / s2);
            if (w < 1.0e-3f)
                continue;
            auto& cu = u[(size_t) index (i, j)];
            auto& cv = v[(size_t) index (i, j)];
            cu += (vel.x - cu) * std::min (1.0f, w);
            cv += (vel.y - cv) * std::min (1.0f, w);
        }
}

void Medium::pulse (juce::Point<float> p, float strength, float radius)
{
    if (nx == 0)
        return;
    const float gx = p.x / cell + 0.5f, gy = p.y / cell + 0.5f, r = radius / cell;
    const int i0 = std::max (1, (int) (gx - r - 1.0f)), i1 = std::min (nx, (int) (gx + r + 1.0f));
    const int j0 = std::max (1, (int) (gy - r - 1.0f)), j1 = std::min (ny, (int) (gy + r + 1.0f));
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i)
        {
            const float dx = (float) i - gx, dy = (float) j - gy;
            const float d = std::sqrt (dx * dx + dy * dy);
            if (d < 1.0e-3f || d > r)
                continue;
            const float w = strength * std::sin (juce::MathConstants<float>::pi * d / r); // a ring, quiet at the centre and the edge
            u[(size_t) index (i, j)] += w * dx / d;
            v[(size_t) index (i, j)] += w * dy / d;
        }
}

void Medium::boundary (std::vector<float>& f, int kind)
{
    // kind 1: horizontal velocity (reflect at the side walls), 2: vertical, 0: scalar
    for (int j = 1; j <= ny; ++j)
    {
        f[(size_t) index (0, j)] = kind == 1 ? -f[(size_t) index (1, j)] : f[(size_t) index (1, j)];
        f[(size_t) index (nx + 1, j)] = kind == 1 ? -f[(size_t) index (nx, j)] : f[(size_t) index (nx, j)];
    }
    for (int i = 1; i <= nx; ++i)
    {
        f[(size_t) index (i, 0)] = kind == 2 ? -f[(size_t) index (i, 1)] : f[(size_t) index (i, 1)];
        f[(size_t) index (i, ny + 1)] = kind == 2 ? -f[(size_t) index (i, ny)] : f[(size_t) index (i, ny)];
    }
    f[(size_t) index (0, 0)] = 0.5f * (f[(size_t) index (1, 0)] + f[(size_t) index (0, 1)]);
    f[(size_t) index (0, ny + 1)] = 0.5f * (f[(size_t) index (1, ny + 1)] + f[(size_t) index (0, ny)]);
    f[(size_t) index (nx + 1, 0)] = 0.5f * (f[(size_t) index (nx, 0)] + f[(size_t) index (nx + 1, 1)]);
    f[(size_t) index (nx + 1, ny + 1)] = 0.5f * (f[(size_t) index (nx, ny + 1)] + f[(size_t) index (nx + 1, ny)]);
}

float Medium::sample (const std::vector<float>& f, float x, float y) const
{
    x = std::clamp (x, 0.5f, (float) nx + 0.5f);
    y = std::clamp (y, 0.5f, (float) ny + 0.5f);
    const int i0 = (int) x, j0 = (int) y;
    const float s = x - (float) i0, t = y - (float) j0;
    return (1.0f - s) * ((1.0f - t) * f[(size_t) index (i0, j0)] + t * f[(size_t) index (i0, j0 + 1)])
           + s * ((1.0f - t) * f[(size_t) index (i0 + 1, j0)] + t * f[(size_t) index (i0 + 1, j0 + 1)]);
}

void Medium::advect (std::vector<float>& out, const std::vector<float>& in, const std::vector<float>& uu, const std::vector<float>& vv,
                     float dt, int kind)
{
    const float d = dt / cell;
    for (int j = 1; j <= ny; ++j)
        for (int i = 1; i <= nx; ++i)
        {
            const size_t k = (size_t) index (i, j);
            out[k] = sample (in, (float) i - d * uu[k], (float) j - d * vv[k]);
        }
    boundary (out, kind);
}

void Medium::project()
{
    // make the velocity divergence-free: what flows in somewhere flows out somewhere else
    for (int j = 1; j <= ny; ++j)
        for (int i = 1; i <= nx; ++i)
        {
            divergence[(size_t) index (i, j)] = -0.5f * (u[(size_t) index (i + 1, j)] - u[(size_t) index (i - 1, j)]
                                                        + v[(size_t) index (i, j + 1)] - v[(size_t) index (i, j - 1)]);
            pressure[(size_t) index (i, j)] = 0.0f;
        }
    boundary (divergence, 0);
    boundary (pressure, 0);
    for (int iteration = 0; iteration < 14; ++iteration)
    {
        for (int j = 1; j <= ny; ++j)
            for (int i = 1; i <= nx; ++i)
                pressure[(size_t) index (i, j)] = (divergence[(size_t) index (i, j)] + pressure[(size_t) index (i - 1, j)]
                                                   + pressure[(size_t) index (i + 1, j)] + pressure[(size_t) index (i, j - 1)]
                                                   + pressure[(size_t) index (i, j + 1)]) * 0.25f;
        boundary (pressure, 0);
    }
    for (int j = 1; j <= ny; ++j)
        for (int i = 1; i <= nx; ++i)
        {
            u[(size_t) index (i, j)] -= 0.5f * (pressure[(size_t) index (i + 1, j)] - pressure[(size_t) index (i - 1, j)]);
            v[(size_t) index (i, j)] -= 0.5f * (pressure[(size_t) index (i, j + 1)] - pressure[(size_t) index (i, j - 1)]);
        }
    boundary (u, 1);
    boundary (v, 2);
}

void Medium::step (float dt, float time, float swirl, float tide)
{
    if (nx == 0 || dt <= 0.0f)
        return;

    // the tide: one or two wide gyres drifting across the window over 20-40 s
    if (tide > 0.0f)
    {
        const float k = tide * dt * 1.5f;
        const float a = 0.0048f, b = 0.0061f;
        for (int j = 1; j <= ny; ++j)
            for (int i = 1; i <= nx; ++i)
            {
                const float x = (float) i * cell, y = (float) j * cell;
                const float p = a * x + 0.19f * time, q = b * y - 0.15f * time + 0.7f;
                // psi = sin p * sin q;  (u, v) = (d psi/dy, -d psi/dx), normalised to ~1
                u[(size_t) index (i, j)] += k * std::sin (p) * std::cos (q);
                v[(size_t) index (i, j)] -= k * std::cos (p) * std::sin (q) * (a / b);
            }
    }

    // ambient curl noise: the curl of a smooth scalar field is divergence-free, so it
    // swirls without bunching up (Bridson's curl noise, here with a few drifting waves)
    if (swirl > 0.0f)
    {
        const float k = swirl * dt * 1.5f;
        for (int j = 1; j <= ny; ++j)
            for (int i = 1; i <= nx; ++i)
            {
                const float x = (float) i * cell, y = (float) j * cell;
                // psi = sin(a x + t) cos(b y - t) + ...;  (u, v) = (d psi/dy, -d psi/dx)
                const float a1 = 0.011f, b1 = 0.014f, a2 = 0.023f, b2 = 0.019f;
                const float p1 = a1 * x + 0.21f * time, q1 = b1 * y - 0.17f * time;
                const float p2 = a2 * x - 0.13f * time + 2.0f, q2 = b2 * y + 0.29f * time + 1.0f;
                const float dpsiDy = -b1 * std::sin (p1) * std::sin (q1) - 0.6f * b2 * std::cos (p2) * std::cos (q2);
                const float dpsiDx = a1 * std::cos (p1) * std::cos (q1) - 0.6f * a2 * std::sin (p2) * std::sin (q2);
                u[(size_t) index (i, j)] += k * dpsiDy / b1;
                v[(size_t) index (i, j)] -= k * dpsiDx / a1;
            }
    }

    project();
    u0 = u;
    v0 = v;
    advect (u, u0, u0, v0, dt, 1);
    advect (v, v0, u0, v0, dt, 2);
    project();

    // the medium is viscous: motion dies away over about a second
    const float keep = std::exp (-1.1f * dt);
    for (auto& x : u)
        x *= keep;
    for (auto& x : v)
        x *= keep;
}

juce::Point<float> Medium::velocity (juce::Point<float> p) const
{
    if (nx == 0)
        return {};
    const float x = p.x / cell + 0.5f, y = p.y / cell + 0.5f;
    return { sample (u, x, y), sample (v, x, y) };
}
} // namespace dendrite::ui
