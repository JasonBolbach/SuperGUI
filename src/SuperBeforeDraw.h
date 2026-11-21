#pragma once
// SuperBeforeDraw - the "BeforeDraw" pull event shared by the display-only controls (SuperDial, SuperGauge, SuperGraph).
//
//   control.SetOnBeforeDraw(fn, user);   // void fn(void* user): set the control's values here (SetValue, AddSample, SetPointerValue, ...)
//   control.SetPollMs(ms);               // how often fn is called from Tick() / Update(); 0 = on every service pass
//
// The first call is at once. While fn runs the control does not draw (SetValue(), AddSample() and the like only mark it changed), and afterwards
// the control redraws once, and only if fn changed something. It is a pull model: the control asks for its values, you do not have to push them.
#include <cstdint>

struct SuperBeforeDraw {
    void (*Fn)(void*) = nullptr;
    void* User = nullptr;
    int   PollMs = 0;
    bool  Running = false;                  // true while Fn runs: the control suppresses its own immediate redraw

    // Called from the control's Tick(). True when Fn ran now.
    bool Run(uint32_t now) {
        if (!Fn) return false;
        if (PollMs > 0 && Polled && now - Last < (uint32_t)PollMs) return false;
        Last = now; Polled = true;
        Running = true; Fn(User); Running = false;
        return true;
    }
private:
    uint32_t Last = 0;
    bool     Polled = false;
};
