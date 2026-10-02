// Procedural vector shapes for UI that has no original art (new options rows, labels, ...).
#pragma once
#include <cmath>

#include "movieclip.h"

namespace sbso::engine {

inline SolidPath round_rect(float x, float y, float w, float h, float r, float cr, float cg, float cb, float ca = 1.0f) {
    SolidPath p;
    p.rgba[0] = cr; p.rgba[1] = cg; p.rgba[2] = cb; p.rgba[3] = ca;
    std::vector<Point> c;
    const int steps = 6;
    auto arc = [&](float cx, float cy, float a0) {
        for (int i = 0; i <= steps; ++i) {
            float a = a0 + 1.5707963f * i / steps;
            c.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
        }
    };
    arc(x + w - r, y + r, -1.5707963f);   // top right
    arc(x + w - r, y + h - r, 0.0f);      // bottom right
    arc(x + r, y + h - r, 1.5707963f);    // bottom left
    arc(x + r, y + r, 3.1415926f);        // top left
    p.contours.push_back(std::move(c));
    return p;
}

}  // namespace sbso::engine
