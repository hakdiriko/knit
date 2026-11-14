// The medium the field floats in: a coarse 2D fluid (Jos Stam's "stable fluids":
// semi-Lagrangian advection and a pressure projection that keeps it incompressible).
//
// Dragging the light stirs it, every hit pushes a ring of it outwards, and a slow curl
// noise keeps it moving when nothing happens. Everything else in the window (the tissue,
// the growth) is carried by it, so one gesture moves the whole field coherently: a
// strand far from the light still feels the wake, a little later and a little less.
//
// UI thread only. About 5,000 cells, a few hundred microseconds per frame.
#pragma once

#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace dendrite::ui
{
class Medium
{
public:
    // The field's size in pixels; cellSize sets the grid's resolution.
    void setSize (float width, float height, float cellSize = 10.0f);

    // Stir: drag the fluid at `p` towards velocity `v` (px/s), within `radius`.
    void stir (juce::Point<float> p, juce::Point<float> v, float radius, float amount);

    // A pulse: push the fluid outwards from `p` (positive) or suck it in (negative).
    void pulse (juce::Point<float> p, float strength, float radius);

    // Advance by dt seconds; `swirl` is the strength of the ambient curl noise (px/s), `tide`
    // of a much wider, slower swell under it (half-minute periods, whole-window wide).
    void step (float dt, float time, float swirl, float tide = 0.0f);

    // The fluid's velocity at a point, in px/s.
    juce::Point<float> velocity (juce::Point<float> p) const;

private:
    int index (int i, int j) const noexcept { return i + (nx + 2) * j; }
    void boundary (std::vector<float>& field, int kind);
    void advect (std::vector<float>& out, const std::vector<float>& in, const std::vector<float>& u, const std::vector<float>& v, float dt, int kind);
    void project();
    float sample (const std::vector<float>& field, float x, float y) const;

    int nx = 0, ny = 0;
    float cell = 10.0f;
    std::vector<float> u, v, u0, v0, pressure, divergence;
};
} // namespace dendrite::ui
