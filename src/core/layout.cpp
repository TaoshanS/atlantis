#include "layout.h"

#include <algorithm>
#include <cmath>

namespace sbso {

Layout compute_layout(int win_w, int win_h, Insets in) {
    Layout l;
    if (win_w <= 0 || win_h <= 0) return l;
    float scale_h = static_cast<float>(win_h) / kDesignH;
    float scale_w = static_cast<float>(win_w) / kDesignW;
    l.scale = std::min(scale_h, scale_w);
    l.int_scale = std::max(1, static_cast<int>(std::floor(l.scale)));
    l.logical_w = std::min(kMaxLogicalW, std::max(kDesignW, static_cast<int>(std::ceil(win_w / l.scale))));
    float bar = std::max(0.0f, (win_w / l.scale - l.logical_w) * 0.5f);  // side bars of a window wider than 2.2:1 absorb the insets
    l.ui_left = std::max(0.0f, in.left / l.scale - bar);
    l.ui_right = l.logical_w - std::max(0.0f, in.right / l.scale - bar);
    l.ui_top = in.top / l.scale;
    l.ui_bottom = kDesignH - in.bottom / l.scale;
    return l;
}

float anchor_x(const Layout& l, Anchor a, float design_x, float element_w) {
    switch (a) {
        case Anchor::Left: return l.ui_left + design_x;
        case Anchor::Right: return l.ui_right - (kDesignW - design_x);
        case Anchor::Center: return (l.logical_w - kDesignW) * 0.5f + design_x;
    }
    (void)element_w;
    return design_x;
}

}  // namespace sbso
