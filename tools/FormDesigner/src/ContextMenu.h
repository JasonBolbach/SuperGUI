#pragma once
// A small pop-up (right-click) menu for the Form Designer, drawn inside the designer window at the mouse position.
//   Rows: a label, an id returned when chosen, enabled or greyed; a row with an empty label is a separator line.
// Click a row (or Up / Down / Enter), Esc or a click anywhere else closes it. The click that closes it is used up (it does not reach the form).
#include <string>
#include <vector>
#include <LovyanGFX.hpp>
#include "DesignerInput.h"
#include "DesignerConfig.h"
#include "DesignerWidgets.h"

namespace FD {
class ContextMenu {
public:
    enum State { OPEN, CHOSEN, CANCEL } state = OPEN;
    int chosen = -1;                                          // the id of the chosen row (state == CHOSEN)

    struct Row { std::string label; int id; bool enabled; };

    // `at` x / y = where the mouse is; the menu is kept inside the window (it opens left / up of the point when it would not fit).
    ContextMenu(lgfx::LGFX_Sprite* g, int atX, int atY, const std::vector<Row>& rows) : G(g), Rows(rows) {
        G->setFont(&lgfx::fonts::Font2);
        W = 0; H = 4;
        for (const Row& r : Rows) { if (!r.label.empty()) W = std::max(W, (int)G->textWidth(r.label.c_str())); H += r.label.empty() ? SEP_H : ROW_H; }
        W += 2 * PAD_X;
        X = atX + W > WIN_W ? std::max(0, atX - W) : atX;
        Y = atY + H > WIN_H ? std::max(0, atY - H) : atY;
        Hot = -1;
    }

    int Left() const { return X; }
    int Top() const { return Y; }
    int Width() const { return W; }
    int Height() const { return H; }
    int RowCount() const { return (int)Rows.size(); }
    int RowY(int i) const { int y = Y + 2; for (int k = 0; k < i; ++k) y += Rows[(size_t)k].label.empty() ? SEP_H : ROW_H; return y; }   // the top of row i
    int RowH(int i) const { return Rows[(size_t)i].label.empty() ? SEP_H : ROW_H; }
    bool Inside(int x, int y) const { return x >= X && y >= Y && x < X + W && y < Y + H; }
    int HotRow() const { return Hot; }

    void Event(const InputEvent& e) {
        if (state != OPEN) return;
        switch (e.type) {
        case InputEvent::MOVE: Hot = Inside(e.x, e.y) ? RowAt(e.y) : -1; break;
        case InputEvent::DOWN:
            if (e.button == SDL_BUTTON_LEFT && Inside(e.x, e.y)) { Hot = RowAt(e.y); Choose(Hot); }
            else state = CANCEL;                              // any other click closes it
            break;
        case InputEvent::KEY_DOWN:
            if (e.key == SDLK_ESCAPE) state = CANCEL;
            else if (e.key == SDLK_DOWN) Step(1);
            else if (e.key == SDLK_UP) Step(-1);
            else if (e.key == SDLK_RETURN || e.key == SDLK_KP_ENTER) Choose(Hot);
            break;
        default: break;
        }
    }

    void Draw() {
        G->setFont(&lgfx::fonts::Font2); G->setTextDatum(lgfx::textdatum_t::top_left);
        G->fillRect(X + 3, Y + 3, W, H, 0x0000);                                    // a simple shadow
        G->fillRect(X, Y, W, H, COL_PANEL); G->drawRect(X, Y, W, H, COL_ACCENT);
        for (int i = 0; i < RowCount(); ++i) {
            const Row& r = Rows[(size_t)i]; int y = RowY(i);
            if (r.label.empty()) { G->drawFastHLine(X + 6, y + SEP_H / 2, W - 12, COL_LINE); continue; }
            if (i == Hot && r.enabled) G->fillRect(X + 2, y, W - 4, ROW_H, COL_SEL);
            G->setTextColor(r.enabled ? 0xFFFF : COL_DIM); G->drawString(r.label.c_str(), X + PAD_X, y + (ROW_H - G->fontHeight()) / 2);
        }
    }

    static constexpr int ROW_H = 24, SEP_H = 9, PAD_X = 14;

private:
    lgfx::LGFX_Sprite* G; std::vector<Row> Rows; int X = 0, Y = 0, W = 0, H = 0, Hot = -1;
    int RowAt(int y) const { for (int i = 0; i < RowCount(); ++i) if (y >= RowY(i) && y < RowY(i) + RowH(i)) return Rows[(size_t)i].label.empty() ? -1 : i; return -1; }
    void Choose(int i) { if (i >= 0 && i < RowCount() && Rows[(size_t)i].enabled && !Rows[(size_t)i].label.empty()) { chosen = Rows[(size_t)i].id; state = CHOSEN; } }
    void Step(int d) {                                                               // keyboard: the next enabled row
        int n = RowCount(); if (n == 0) return; int i = Hot;
        for (int k = 0; k < n; ++k) { i = i < 0 ? (d > 0 ? 0 : n - 1) : (i + d + n) % n; if (!Rows[(size_t)i].label.empty() && Rows[(size_t)i].enabled) { Hot = i; return; } }
    }
};
}
