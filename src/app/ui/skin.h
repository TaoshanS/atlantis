// The original UI look for the screens this port adds (saved games, language/graphics rows, extra buttons): the bamboo panel is cut from the
// original menu_assets bitmap, buttons are cut from the original OPTIONS/QUIT bitmaps, text is white TikiMagic with a dark outline.
#pragma once
#include <deque>
#include <string>
#include <vector>

#include "app/app.h"
#include "engine/shapes.h"

namespace sbso::app {

class Skin {
public:
    explicit Skin(App& app) : app_(app) {}
    void begin() { shapes_.clear(); prev_hover_ = cur_hover_; cur_hover_ = 0; ++phase_; }  // call first in draw(): the shapes of the previous frame are released

    // Bamboo frame + olive fill + darker inner plate over the outer rectangle `r`. Returns the plate (the area for content).
    engine::Rect panel(std::vector<engine::DrawCmd>& out, const engine::Rect& r);
    // Inner plate of a panel of outer size r (without drawing).
    static engine::Rect plate_of(const engine::Rect& r);
    // The original green menu button (red variant like QUIT), built from its bitmap. Label centred. The hover brackets go to
    // `brackets_out` when given, so a caller can draw them above neighbouring original buttons.
    void button(std::vector<engine::DrawCmd>& out, const engine::Rect& r, const std::string& label, bool hover, bool enabled = true, bool red = false,
                float size = 14, std::vector<engine::DrawCmd>* brackets_out = nullptr);
    // A rounded plate (list rows): `shade` darkens (>0) or lightens (<0) the plate colour.
    void plate(std::vector<engine::DrawCmd>& out, const engine::Rect& r, float radius, float shade);
    // White label with the original dark drop shadow.
    void text(std::vector<engine::DrawCmd>& out, const std::string& s, float x, float y, float size, int align = 0, float box_w = 0, bool dim = false);

private:
    engine::SolidPath* add(const engine::SolidPath& p);
    void solid(std::vector<engine::DrawCmd>& out, const engine::SolidPath& p);
    App& app_;
    int prev_hover_ = 0, cur_hover_ = 0, phase_ = 0;
    unsigned long long hover_since_ns_ = 0;  // hover sound on entering a button; phase drives the bracket bounce
    std::deque<engine::SolidPath> shapes_;
};

}  // namespace sbso::app
