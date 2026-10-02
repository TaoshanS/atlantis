// "Saved games": the autosave and the manual save slots of the active profile (an addition to the original game).
#pragma once
#include <deque>
#include <functional>
#include <vector>

#include "app/app.h"
#include "app/ui/skin.h"
#include "engine/shapes.h"

namespace sbso::app {

class SavesScreen : public Screen {
public:
    SavesScreen(App& app, std::function<void()> on_loaded);
    void tick(App&) override {}
    void draw(App& app, std::vector<engine::DrawCmd>& out) override;
    void pointer_move(App& app, engine::Point p) override { pointer_ = p; }
    void pointer_down(App&, engine::Point) override {}
    void pointer_up(App& app, engine::Point p) override;
    void key_down(App& app, int key) override;

private:
    struct Button { engine::Rect r; std::string label; int action = 0; int slot = 0; bool enabled = true; };
    void rebuild();
    App& app_;
    Skin skin_;
    std::function<void()> on_loaded_;
    std::vector<save::SlotInfo> slots_;
    std::vector<Button> buttons_;
    engine::Point pointer_{-1000, -1000};
    std::string message_;
};

}  // namespace sbso::app
