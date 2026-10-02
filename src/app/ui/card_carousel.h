// Port of CardCarousel.as: the four-icon card type wheel with left/right arrows (used by the treasure chest deck screen).
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/app.h"

namespace sbso::app {

class CardCarousel {
public:
    explicit CardCarousel(App& app);
    bool valid() const { return built_; }
    void tick();
    // origin: where the carousel's own (0,0) lands on screen; scale: popup animation factor.
    void draw(std::vector<engine::DrawCmd>& out, const engine::Matrix& origin, float scale = 1.0f) const;
    bool pointer_down(engine::Point local);  // local = relative to the carousel origin; true if an arrow was pressed
    void set_type(int type_index);           // turn the wheel to MOVE/ATTACK/DEFEND/NICK (0..3) without an event
    int type() const { return current_; }
    std::function<void(int)> on_type;        // fired when an arrow turns the wheel

private:
    void set_angle(double a);
    void press(bool right);
    App& app_;
    const engine::Library* lib_ = nullptr;
    std::unique_ptr<engine::MovieClip> clips_[4], arrows_;
    double angle_ = 0, target_ = 0, force_ = 0;
    int counter_ = 0, num_updates_ = 16, current_ = 0;
    bool turning_left_ = false, turning_right_ = false, built_ = false;
};

}  // namespace sbso::app
