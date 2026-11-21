#pragma once
// SuperDialDraw - the drawing helpers shared by the round controls (SuperGauge, SuperDial).
//
// Angles are in degrees, measured clockwise from 12 o'clock (LovyanGFX counts from 3 o'clock; DialArc converts).
// The helpers take the canvas (a LovyanGFX sprite or display) as their first argument, so they have no state of their own.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <lgfx/v1/LGFX_Sprite.hpp>

namespace SuperDialDraw {

constexpr float DEG2RAD = 0.0174533f;

// The point at 'radius' from (cx, cy) in direction 'deg' (clockwise from 12 o'clock): the unit vector is (sin, -cos) in screen coordinates.
struct Pt { float x, y; };
inline Pt Polar(int cx, int cy, float radius, float deg) { float t = deg * DEG2RAD; return { cx + std::sin(t) * radius, cy - std::cos(t) * radius }; }

// The angle of value v on a scale that maps vMin .. vMax to startDeg .. endDeg.
inline float ValueToAngle(float v, float vMin, float vMax, float startDeg, float endDeg) { return startDeg + (endDeg - startDeg) * (v - vMin) / (vMax - vMin); }

// Filled ring segment between two radii from angle a0 to a1; a span of a full turn or more draws the whole ring.
template <class Canvas>
inline void Arc(Canvas& g, int cx, int cy, int r0, int r1, float a0, float a1, uint16_t colour) {
    auto norm = [](float a) { a = std::fmod(a - 90.f, 360.f); return a < 0 ? a + 360.f : a; };
    float s = norm(a0), e = norm(a1);
    if (a1 - a0 >= 359.9f) { s = 0; e = 360; }
    g.fillArc(cx, cy, r0, r1, s, e, colour);
}

// A number as text: no decimals (rounded) or 'decimals' decimals.
inline void FormatNumber(float v, char* b, size_t n, int decimals) {
    if (decimals <= 0) snprintf(b, n, "%d", (int)std::lround(v)); else snprintf(b, n, "%.*f", decimals, (double)v);
}

}  // namespace SuperDialDraw
