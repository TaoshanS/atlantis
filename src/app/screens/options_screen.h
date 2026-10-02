// Port of OptionsScreen.as (sound/music sliders, fullscreen) plus two new rows: language and graphics filter.
#pragma once
#include <memory>
#include <vector>

#include "app/app.h"
#include "app/ui/skin.h"
#include "engine/shapes.h"

namespace sbso::app {

class OptionsScreen : public Screen {
public:
    explicit OptionsScreen(App& app);
    void tick(App& app) override;
    void draw(App& app, std::vector<engine::DrawCmd>& out) override;
    void pointer_move(App& app, engine::Point p) override;
    void pointer_down(App& app, engine::Point p) override;
    void pointer_up(App& app, engine::Point p) override;
    void key_down(App& app, int key) override;

private:
    struct Thumb {
        std::string thumb, bar, under;
        bool animating = false;
        float a = 0, b = 0, prog = 0, inc = 0;
    };
    struct Choice {  // one button of the language / graphics rows
        std::string label;
        std::string value;
        engine::Rect rect;
        bool selected = false;
    };

    void set_thumb(int i, float x);
    void init_thumb_anim(int i, float target);
    void apply_volume(int i);
    float bg_x(engine::Point p) const { return p.x - origin_.x; }
    float bg_y(engine::Point p) const { return p.y - origin_.y; }
    void close(App& app);
    void rebuild_choices(App& app);
    void checkbox_geometry(engine::Matrix* m, engine::Rect* hit) const;

    App& app_;
    const engine::Library* lib_ = nullptr;
    std::unique_ptr<engine::MovieClip> bg_, checkbox_on_, checkbox_off_;
    engine::Point origin_{321, 239};
    Thumb thumbs_[2];
    int dragging_ = -1;
    float drag_offset_ = 0;
    bool intro_ = true;
    engine::Point pointer_{-1000, -1000};
    bool pointer_down_ = false;
    std::string hover_;
    // extra rows
    std::vector<Choice> language_, graphics_;
    Skin skin_;
    std::vector<engine::SolidPath> shapes_;  // the dimming rectangle
    static engine::Rect extra_panel();  // outer rectangle of the language / graphics panel
    std::string language_value_, graphics_value_;
    float bounds_left_ = -69, bounds_right_ = 195;
};

}  // namespace sbso::app
