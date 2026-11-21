#pragma once
// SuperDial - a round face with any number of pointers, display only (like SuperGauge: form.Add(&dial, false)).
// A clock face, a compass, a circular gauge: they differ only in the scale, the labels and the pointers you give them.
//
//   SuperDial d(&lcd, 10, 10, 200, /*body height*/ 200, &lgfx::fonts::Font2);
//   d.SetSweep(0, 100, -135, 135);                       // a 270 degree gauge: value 0..100 over -135..+135 degrees
//   d.AddZone(0, 60, TFT_GREEN);  d.AddZone(60, 100, TFT_RED);
//   d.AddPointer();                                      // pointer 0 (length 0.9 of the dial, taper style)
//   d.SetPointerValue(0, 42);                            // it glides there
//
//   Compass: d.SetWrap(360);  d.SetTicks(8, 2);  d.SetTickLabels({"N","NE","E","SE","S","SW","W","NW"});
//            d.AddPointer(0.9f, 5, TFT_RED);  d.SetFaceRotation(-heading);      // the card turns, the pointer stays at the top
//   Clock:   d.SetWrap(12);   d.SetTicks(12, 4);  d.AddPointer(0.5f, 6);  d.AddPointer(0.8f, 4);  d.AddPointer(0.9f, 2, TFT_RED);
//
//  * Scale: SetSweep(min, max, startDeg, endDeg) maps a value range onto an arc (degrees clockwise from 12 o'clock; values are clamped).
//    SetWrap(range) maps 0 .. range onto the whole circle and the values wrap round (359 -> 1 turns through 0, not back through 180).
//  * Pointers: AddPointer(length, width, colour, style, faceRelative) returns its index. Length is a fraction (0..1) of the room inside the
//    ticks. POINTER_TAPER (a wedge), POINTER_LINE (a bar) or POINTER_TAIL (a wedge with a short tail behind the hub). A pointer that is
//    faceRelative turns with the face (a bearing on the card); the others stay where the scale puts them on the screen.
//  * Face: the rim, the zone ring (AddZone: in wrap mode a zone with from > to crosses the top), the ticks and the labels. SetFaceRotation(deg)
//    turns all of that clockwise; the labels move with it but stay upright. SetTickLabels() replaces the numbers at the major ticks (an empty
//    string leaves a tick bare). Without custom labels a wrap scale shows its numbers with the top tick showing the range's end (12, not 0).
//  * Hub: ShowValue (default) writes the value of pointer ValuePointer (default 0) in a box over the hub, with SetFormat(decimals, unit), or
//    the text of SetHubText(). ValueInHub = false puts it in a strip under the dial. SetHubText("") returns to the pointer's value.
//  * Smoothing (default ON): SetPointerValue() and SetFaceRotation() start a glide of about SmoothMs; SuperForm::Service() drives it through
//    Tick(), without a form call Update() from loop(). The dial only redraws while something moves.
//  * BeforeDraw (pull model): SetOnBeforeDraw(fn, user) calls your function from Tick() / Update() every SetPollMs() milliseconds (0 = every service pass)
//    and the dial redraws only if the function changed something. Put the code that reads the clock / sensor in it: SetPointerValue(), SetTime(),
//    SetHeading(), SetFaceRotation(), SetHubText(), or ValuePointer = n. Nothing is drawn while it runs (no second repaint), and a call that sets
//    the same values again costs nothing.
//  * Presets: MakeClock() (SetTime(h, m, s)) and MakeCompass() (SetHeading(deg)) set the scale, ticks, labels and pointers of those two faces.
//  * All the maths is single precision (float): the face's sines and cosines are worked out once per tick set and rotated per frame.
#include "SuperControl.h"
#include "SuperDialDraw.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

class SuperDial : public SuperControl {
public:
    ControlType GetControlType() const override { return ControlType::Dial; }
    enum PointerStyle : uint8_t { POINTER_TAPER = 0, POINTER_LINE, POINTER_TAIL };

    uint16_t ColFace  = 0x0000;            // the face inside the rim
    uint16_t ColRim   = 0x7BEF;            // the rim circle
    uint16_t ColTick  = 0x7BEF;            // tick marks and labels
    uint16_t ColHub   = 0xFFFF;            // the hub dot (and the box outline)
    bool     ShowFace = true;              // fill the face with ColFace (off: the control's background shows through)
    bool     ShowRim  = true;
    bool     ShowValue      = true;        // the value of pointer ValuePointer as text
    bool     ValueInHub     = true;        // in a box over the hub; false = in a strip under the dial
    bool     ShowTickLabels = true;        // numbers / labels at the major ticks (hidden automatically when the dial is small)
    int      ValuePointer   = 0;           // which pointer's value the hub shows
    int      SmoothMs       = 250;         // about how long a pointer takes to reach a new value
    int      ZoneThickness  = 0;           // the zone ring in pixels (0 = a tenth of the radius)
    const lgfx::IFont* TickFont = &lgfx::fonts::Font0;    // font of the tick labels

    SuperDial(lgfx::LovyanGFX* dst, int x, int y, int width, int bodyHeight, const lgfx::IFont* font = &lgfx::fonts::Font2)
        : SuperControl(dst, x, y, width, font), BodyH(bodyHeight < 16 ? 16 : bodyHeight) {}

    // ---- scale ----
    void SetSweep(float mn, float mx, float startDeg = -135.f, float endDeg = 135.f) {
        Min = mn; Max = mx > mn ? mx : mn + 1.f; Start = startDeg; End = endDeg > startDeg ? endDeg : startDeg + 1.f; Wrap = false;
        for (auto& p : Ptrs) { p.Value = std::clamp(p.Value, Min, Max); p.Disp = std::clamp(p.Disp, Min, Max); }
        TicksDirty = true; Dirty = true;
    }
    void SetWrap(float range, float startDeg = 0.f) {                       // 0 .. range round the whole circle, 'startDeg' = where 0 sits
        Min = 0.f; Max = range > 0.f ? range : 360.f; Start = startDeg; End = startDeg + 360.f; Wrap = true;
        for (auto& p : Ptrs) { p.Value = WrapValue(p.Value); p.Disp = WrapValue(p.Disp); }
        TicksDirty = true; Dirty = true;
    }
    void SetTicks(int major, int minor) { MajorN = std::clamp(major, 1, 120); MinorN = std::clamp(minor, 0, 11); TicksDirty = true; Dirty = true; }
    void SetTickLabels(const std::vector<std::string>& labels) { Labels = labels; Dirty = true; }
    void ClearTickLabels() { Labels.clear(); Dirty = true; }
    void SetFormat(int decimals, const std::string& unit = "") { decimals = std::clamp(decimals, 0, 4); if (decimals == Decimals && unit == Unit) return; Decimals = decimals; Unit = unit; Dirty = true; }
    void SetHubText(const std::string& t) { if (t == HubText) return; HubText = t; Dirty = true; }
    void SetValuePointer(int i) { if (i == ValuePointer) return; ValuePointer = i; Dirty = true; }


    void AddZone(float from, float to, uint16_t colour) { Zones.push_back({ from, to, colour }); Dirty = true; }
    void ClearZones() { Zones.clear(); Dirty = true; }
    void SetBodyHeight(int h) { BodyH = h < 16 ? 16 : h; Dirty = true; }
    bool IsWrap() const { return Wrap; }
    float GetMin() const { return Min; }
    float GetMax() const { return Max; }

    // ---- pointers ----
    int AddPointer(float length = 0.9f, int width = 4, uint16_t colour = 0xFFFF, PointerStyle style = POINTER_TAPER, bool faceRelative = false) {
        Pointer p; p.Length = std::clamp(length, 0.05f, 1.f); p.Width = width < 1 ? 1 : width; p.Colour = colour; p.Style = style; p.FaceRelative = faceRelative;
        p.Value = p.Disp = Min; Ptrs.push_back(p); Dirty = true; return (int)Ptrs.size() - 1;
    }
    void ClearPointers() { Ptrs.clear(); Dirty = true; }
    int  PointerCount() const { return (int)Ptrs.size(); }
    void SetPointerStyle(int i, float length, int width, uint16_t colour, PointerStyle style = POINTER_TAPER) {
        if (!Has(i)) return;
        Pointer& p = Ptrs[(size_t)i]; p.Length = std::clamp(length, 0.05f, 1.f); p.Width = width < 1 ? 1 : width; p.Colour = colour; p.Style = style; Dirty = true;
    }
    void SetPointerVisible(int i, bool v) { if (Has(i)) { Ptrs[(size_t)i].Visible = v; Dirty = true; } }
    void SetPointerValue(int i, float v) {
        if (!Has(i)) return;
        Pointer& p = Ptrs[(size_t)i];
        v = Wrap ? WrapValue(v) : std::clamp(v, Min, Max);
        if (v == p.Value && !p.Moving) return;
        p.Value = v;
        if (!Smooth) { p.Disp = v; p.Moving = false; Dirty = true; Refresh(); return; }
        p.Moving = (p.Disp != p.Value); LastMs = lgfx::millis();
    }
    float GetPointerValue(int i) const { return Has(i) ? Ptrs[(size_t)i].Value : 0.f; }
    float GetShownValue(int i) const { return Has(i) ? Ptrs[(size_t)i].Disp : 0.f; }      // where the pointer is right now (differs while it glides)

    // ---- presets and helpers ----
    // A 12 hour clock face: pointer 0 = hours, 1 = minutes, 2 = seconds (added when missing). SetTime() moves them.
    void MakeClock() {
        SetWrap(12.f); SetTicks(12, 4); ClearTickLabels(); ClearZones(); ShowValue = false;
        while (PointerCount() < 3) AddPointer();
        SetPointerStyle(0, 0.5f, 6, 0xFFFF); SetPointerStyle(1, 0.8f, 4, 0xFFFF); SetPointerStyle(2, 0.9f, 2, 0xF800, POINTER_TAIL);
    }
    // A compass card: N E S W, the card turns (SetHeading), pointer 0 stays at the top.
    void MakeCompass() {
        SetWrap(360.f); SetTicks(8, 2); SetTickLabels({ "N", "NE", "E", "SE", "S", "SW", "W", "NW" }); ClearZones(); ShowValue = false;
        if (PointerCount() < 1) AddPointer();
        SetPointerStyle(0, 0.9f, 5, 0xF800, POINTER_TAIL);
    }
    // The hands of a 12 hour clock (MakeClock layout): the hour hand moves smoothly with the minutes, the minute hand with the seconds.
    void SetTime(int hours, int minutes, int seconds) {
        float s = (float)seconds, m = (float)minutes + s / 60.f, h = (float)(hours % 12) + m / 60.f;
        SetPointerValue(0, h * (Max / 12.f)); SetPointerValue(1, m / 5.f * (Max / 12.f)); SetPointerValue(2, s / 5.f * (Max / 12.f));
    }
    // A compass: the card turns so the heading (degrees, 0 = north) is at the top.
    void SetHeading(float degrees) { SetFaceRotation(-degrees); }

    // ---- face rotation (degrees clockwise) ----
    void SetFaceRotation(float deg) {
        deg = Norm360(deg);
        if (deg == FaceValue && !FaceMoving) return;
        FaceValue = deg;
        if (!Smooth) { FaceDisp = deg; FaceMoving = false; Dirty = true; Refresh(); return; }
        FaceMoving = (FaceDisp != FaceValue); LastMs = lgfx::millis();
    }
    float GetFaceRotation() const { return FaceValue; }

    void SetSmoothing(bool on, int ms = 250) {
        Smooth = on; SmoothMs = ms < 1 ? 1 : ms;
        if (!on) { for (auto& p : Ptrs) { p.Disp = p.Value; p.Moving = false; } FaceDisp = FaceValue; FaceMoving = false; Dirty = true; }
    }

    // ---- animation clock (SuperForm calls it; without a form call Update() from loop()) ----
    bool Tick() override {
        if (!IsVisible()) return false;
        uint32_t now = lgfx::millis();
        bool any = FaceMoving; for (auto& p : Ptrs) any = any || p.Moving;
        if (!any) return false;
        if (now - LastDraw < 20) return false;                                  // at most about 50 redraws a second
        float dt = (float)std::min<uint32_t>(now - LastMs, 100u);
        LastMs = now; LastDraw = now;
        float k = 1.f - std::exp(-dt / ((float)SmoothMs * 0.35f));              // about 95% of the way after SmoothMs
        float range = Max - Min;
        for (auto& p : Ptrs) {
            if (!p.Moving) continue;
            float d = p.Value - p.Disp;
            if (Wrap) d = Short(d, range);
            p.Disp += d * k;
            if (Wrap) p.Disp = WrapValue(p.Disp);
            float left = p.Value - p.Disp; if (Wrap) left = Short(left, range);
            if (std::fabs(left) < range * 0.003f) { p.Disp = p.Value; p.Moving = false; }
        }
        if (FaceMoving) {
            float d = Short(FaceValue - FaceDisp, 360.f);
            FaceDisp = Norm360(FaceDisp + d * k);
            if (std::fabs(Short(FaceValue - FaceDisp, 360.f)) < 0.3f) { FaceDisp = FaceValue; FaceMoving = false; }
        }
        Dirty = true;
        return true;
    }

protected:
    void AfterBeforeDraw() override { if (ValuePointer != DrawnVP) Dirty = true; }          // BeforeDraw changed which pointer the hub shows
    int BodyHeight() override { return BodyH; }

    void RenderBody(int x, int y, int w, int h) override {
        using namespace SuperDialDraw;
        Spr.setFont(Font);
        bool inHub = ShowValue && ValueInHub;
        int fh = (ShowValue && !inHub) ? FontH() + 2 : 0;                       // the readout strip at the bottom
        int ah = h - fh, m = 2;
        int boxW = 0, boxH = 0;
        std::string hub = HubString(); DrawnVP = ValuePointer;
        if (inHub) {                                                            // box sized for the widest text the dial can show
            boxW = (int)std::max(Spr.textWidth(hub.c_str()), Spr.textWidth(RangeString(Min).c_str()));
            boxW = std::max(boxW, (int)Spr.textWidth(RangeString(Max).c_str())) + 8;
            boxH = FontH() + 4;
            if (boxW > w * 2 / 3) inHub = false, fh = FontH() + 2, ah = h - fh;
        }
        int cx = x + w / 2, cy = y + ah / 2, rad = std::min(w / 2 - m, ah / 2 - m);
        if (rad < 12) return;                                                   // too small to draw anything sensible
        Spr.setClipRect(x, y, w, h);
        if (ShowFace) Spr.fillCircle(cx, cy, rad, ColFace);
        if (ShowRim) { Spr.drawCircle(cx, cy, rad, ColRim); Spr.drawCircle(cx, cy, rad - 1, ColRim); }

        float fa = FaceDisp, sf = std::sin(fa * DEG2RAD), cf = std::cos(fa * DEG2RAD);      // the face's rotation: one sin / cos per frame
        int outer = rad - 3;

        // the zone ring
        int inner = outer;
        if (!Zones.empty()) {
            int zt = ZoneThickness > 0 ? ZoneThickness : std::max(3, rad / 10);
            inner = outer - zt;
            for (auto& z : Zones) {
                float f = std::clamp(z.From, Min, Max), t = std::clamp(z.To, Min, Max);
                if (Wrap && t < f) { ZoneArc(cx, cy, inner, outer, f, Max, fa, z.Colour); ZoneArc(cx, cy, inner, outer, Min, t, fa, z.Colour); }
                else if (t > f) ZoneArc(cx, cy, inner, outer, f, t, fa, z.Colour);
            }
            inner -= 1;
        }

        // ticks and labels, from the cached unit vectors rotated by the face
        BuildTicks();
        int majorLen = std::max(4, rad / 9), minorLen = std::max(2, majorLen / 2);
        bool labels = ShowTickLabels && rad >= 30;
        int steps = (int)TickS.size();
        int r1 = inner - 2, rLabelMax = r1 - majorLen - 3;
        for (int i = 0; i < steps; ++i) {
            bool major = (i % (MinorN + 1)) == 0;
            int r0 = r1 - (major ? majorLen : minorLen);
            float s = TickS[(size_t)i] * cf + TickC[(size_t)i] * sf, c = TickC[(size_t)i] * cf - TickS[(size_t)i] * sf;     // (sin, cos) of tick angle + face
            Spr.drawLine(cx + (int)std::lround(s * (float)r0), cy - (int)std::lround(c * (float)r0), cx + (int)std::lround(s * (float)r1), cy - (int)std::lround(c * (float)r1), ColTick);
            if (major && labels) {
                std::string lt = LabelFor(i / (MinorN + 1));
                if (lt.empty()) continue;
                Spr.setFont(TickFont); Spr.setTextColor(ColTick); Spr.setTextDatum(lgfx::textdatum_t::middle_center);
                int tw = (int)Spr.textWidth(lt.c_str()), th = Spr.fontHeight();
                float rl = (float)(rLabelMax - (int)(std::fabs(s) * (float)tw * 0.5f + std::fabs(c) * (float)th * 0.5f));     // keep the label clear of its tick
                Spr.drawString(lt.c_str(), cx + (int)std::lround(s * rl), cy - (int)std::lround(c * rl));
                Spr.setFont(Font);
            }
        }

        // the pointers, in order (a later one is on top of an earlier one)
        int reach = std::max(4, inner - 3);
        int hubR = std::max(3, rad / 14);
        for (auto& p : Ptrs) {
            if (!p.Visible) continue;
            float ang = ValueToAngle(p.Disp, Min, Max, Start, End) + (p.FaceRelative ? fa : 0.f);
            DrawPointer(cx, cy, ang, (float)reach * p.Length, p);
        }
        if (inHub) {
            Spr.fillRoundRect(cx - boxW / 2, cy - boxH / 2, boxW, boxH, 3, ColBg);
            Spr.drawRoundRect(cx - boxW / 2, cy - boxH / 2, boxW, boxH, 3, ColHub);
            Spr.setFont(Font); Spr.setTextColor(ColFg); Spr.setTextDatum(lgfx::textdatum_t::middle_center);
            Spr.drawString(hub.c_str(), cx, cy);
        } else {
            Spr.fillCircle(cx, cy, hubR, ColHub);
            Spr.drawCircle(cx, cy, hubR + 1, ColFg);
        }
        if (ShowValue && !inHub) {
            Spr.setFont(Font); Spr.setTextColor(ColFg); Spr.setTextDatum(lgfx::textdatum_t::top_center);
            Spr.drawString(hub.c_str(), cx, y + ah + 1);
        }
        Spr.clearClipRect();
    }

private:
    struct Zone { float From, To; uint16_t Colour; };
    struct Pointer { float Value = 0.f, Disp = 0.f, Length = 0.9f; int Width = 4; uint16_t Colour = 0xFFFF; PointerStyle Style = POINTER_TAPER; bool FaceRelative = false, Visible = true, Moving = false; };
    int   BodyH;
    float Min = 0.f, Max = 100.f, Start = -135.f, End = 135.f;
    bool  Wrap = false;
    int   MajorN = 10, MinorN = 1, Decimals = 0;
    std::string Unit, HubText;
    std::vector<std::string> Labels;
    std::vector<Zone> Zones;
    std::vector<Pointer> Ptrs;
    bool  Smooth = true, FaceMoving = false;
    float FaceValue = 0.f, FaceDisp = 0.f;
    uint32_t LastMs = 0, LastDraw = 0;
    int   DrawnVP = 0;                                                          // the ValuePointer the last picture showed
    bool  TicksDirty = true;
    std::vector<float> TickS, TickC;                                            // sin / cos of each tick's angle with the face unrotated

    bool Has(int i) const { return i >= 0 && i < (int)Ptrs.size(); }
    static float Norm360(float a) { a = std::fmod(a, 360.f); return a < 0.f ? a + 360.f : a; }
    float WrapValue(float v) const { float r = Max - Min; v = std::fmod(v - Min, r); return (v < 0.f ? v + r : v) + Min; }
    static float Short(float d, float range) { d = std::fmod(d, range); if (d > range * 0.5f) d -= range; else if (d < -range * 0.5f) d += range; return d; }    // the short way round

    // a wrap scale has MajorN major ticks round the circle (the last would repeat the first); a sweep has MajorN + 1 (both ends)
    void BuildTicks() {
        if (!TicksDirty) return;
        TicksDirty = false;
        int per = MajorN * (MinorN + 1), n = Wrap ? per : per + 1;
        TickS.assign((size_t)n, 0.f); TickC.assign((size_t)n, 0.f);
        float span = End - Start;
        for (int i = 0; i < n; ++i) {
            float t = (Start + span * (float)i / (float)per) * SuperDialDraw::DEG2RAD;
            TickS[(size_t)i] = std::sin(t); TickC[(size_t)i] = std::cos(t);
        }
    }
    std::string LabelFor(int major) const {
        if (!Labels.empty()) return major < (int)Labels.size() ? Labels[(size_t)major] : std::string();
        float v = Min + (Max - Min) * (float)major / (float)MajorN;
        if (Wrap && major == 0) v = Max;                                        // the top of a clock says 12, not 0
        char b[16]; SuperDialDraw::FormatNumber(v, b, sizeof b, std::fabs(Max - Min) < 10.f ? 1 : 0);
        return b;
    }
    std::string RangeString(float v) const { char b[24]; SuperDialDraw::FormatNumber(v, b, sizeof b, Decimals); return std::string(b) + Unit; }
    std::string HubString() const {
        if (!HubText.empty()) return HubText;
        return RangeString(Has(ValuePointer) ? Ptrs[(size_t)ValuePointer].Value : 0.f);
    }
    void ZoneArc(int cx, int cy, int r0, int r1, float from, float to, float faceDeg, uint16_t colour) {
        SuperDialDraw::Arc(Spr, cx, cy, r0, r1, SuperDialDraw::ValueToAngle(from, Min, Max, Start, End) + faceDeg, SuperDialDraw::ValueToAngle(to, Min, Max, Start, End) + faceDeg, colour);
    }
    // a bar of 'width' pixels from (x0,y0) to (x1,y1)
    void Bar(float x0, float y0, float x1, float y1, float width, uint16_t c) {
        if (width <= 1.5f) { Spr.drawLine((int)std::lround(x0), (int)std::lround(y0), (int)std::lround(x1), (int)std::lround(y1), c); return; }
        float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.5f) return;
        float hx = -dy / len * (width - 1.f) * 0.5f, hy = dx / len * (width - 1.f) * 0.5f;
        int ax = (int)std::lround(x0 - hx), ay = (int)std::lround(y0 - hy), bx = (int)std::lround(x0 + hx), by = (int)std::lround(y0 + hy);
        int cx2 = (int)std::lround(x1 + hx), cy2 = (int)std::lround(y1 + hy), dx2 = (int)std::lround(x1 - hx), dy2 = (int)std::lround(y1 - hy);
        Spr.fillTriangle(ax, ay, bx, by, cx2, cy2, c); Spr.fillTriangle(ax, ay, cx2, cy2, dx2, dy2, c);
    }
    void DrawPointer(int cx, int cy, float deg, float len, const Pointer& p) {
        SuperDialDraw::Pt tip = SuperDialDraw::Polar(cx, cy, len, deg);
        if (p.Style == POINTER_LINE) { Bar((float)cx, (float)cy, tip.x, tip.y, (float)p.Width, p.Colour); return; }
        SuperDialDraw::Pt side = SuperDialDraw::Polar(0, 0, (float)p.Width * 0.5f, deg + 90.f);        // half the base, sideways
        int bx = (int)std::lround((float)cx + side.x), by = (int)std::lround((float)cy + side.y), ex = (int)std::lround((float)cx - side.x), ey = (int)std::lround((float)cy - side.y);
        Spr.fillTriangle((int)std::lround(tip.x), (int)std::lround(tip.y), bx, by, ex, ey, p.Colour);
        if (p.Style == POINTER_TAIL) {
            SuperDialDraw::Pt tail = SuperDialDraw::Polar(cx, cy, -len * 0.2f, deg);
            Spr.fillTriangle((int)std::lround(tail.x), (int)std::lround(tail.y), bx, by, ex, ey, p.Colour);
        }
    }
};
