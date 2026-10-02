// A MovieClip whose buttons (DefineButton records) are clicked with the pointer: hover/press states, rollover sound and
// click detection. Shared by every screen that is a plain clip of named buttons (menus, dialogs, instruction book...).
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/app.h"

namespace sbso::app {

class ClipPanel {
public:
    ClipPanel() = default;
    // `names` lists the instance names of the buttons that report clicks (matched along the display path).
    ClipPanel(std::unique_ptr<engine::MovieClip> clip, engine::Matrix place, std::vector<std::string> names);

    // Names that report clicks; "*" matches any button of the clip. Buttons that are not listed neither hover nor click.
    void set_names(std::vector<std::string> n) { names_ = std::move(n); }
    bool valid() const { return clip_ != nullptr; }
    engine::MovieClip* clip() const { return clip_.get(); }
    void set_place(const engine::Matrix& m) { place_ = m; }
    void tick() { if (clip_) clip_->tick(); }
    engine::Matrix place() const { return place_; }
    void draw(std::vector<engine::DrawCmd>& out) const { if (clip_) clip_->collect(out, place_, engine::ColorTransform{}); }

    void pointer_move(App& app, engine::Point p);
    void pointer_down(App& app, engine::Point p);
    // Returns the name of the clicked button ("" if none). Plays no click sound (callers choose it).
    std::string pointer_up(App& app, engine::Point p);

private:
    std::string name_at(engine::Point p);
    void update_hover(App& app);

    std::unique_ptr<engine::MovieClip> clip_;
    engine::Matrix place_;
    std::vector<std::string> names_;
    engine::Point pointer_{-1000, -1000};
    bool down_ = false;
    std::string hover_, down_name_;
};

}  // namespace sbso::app
