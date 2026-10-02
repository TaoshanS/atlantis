// Screen layout: the world is authored at 480 logical pixels of height (the original game is
// 640x480). Wider screens extend the scene sideways (logical width grows), and UI elements
// are anchored to the left, centre or right edge instead of being letterboxed.
#pragma once

namespace sbso {

constexpr int kDesignW = 640;
constexpr int kDesignH = 480;
// Widest extended scene (2.2:1): beyond this the levels run out of art, so wider windows get side bars.
constexpr int kMaxLogicalW = 1056;

struct Insets {  // safe-area insets in physical pixels (notch, rounded corners, home bar)
    float left = 0, top = 0, right = 0, bottom = 0;
};

struct Layout {
    int logical_w = kDesignW;  // >= 640; logical_h is always 480
    float scale = 1.0f;        // physical pixels per logical pixel (smooth/Lanczos mode)
    int int_scale = 1;         // floor(scale), at least 1 (Original/nearest mode)
    float ui_left = 0;         // safe-area bounds in logical coordinates
    float ui_right = kDesignW;
    float ui_top = 0;
    float ui_bottom = kDesignH;
};

// Never narrower than 4:3: portrait or square windows get side bars of unused space instead
// (scale is limited by width), because the design cannot shrink below 640x480.
Layout compute_layout(int win_w, int win_h, Insets insets = {});

enum class Anchor { Left, Center, Right };

// x of an element authored at design_x (relative to a 640-wide design) after anchoring.
float anchor_x(const Layout& l, Anchor a, float design_x, float element_w);

}  // namespace sbso
