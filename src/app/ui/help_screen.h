// Port of HelpScreen.as: a step-by-step help overlay driven by a timeline in help.swf (Next / Close buttons per step).
#pragma once
#include <functional>
#include <string>

#include "app/app.h"
#include "app/ui/clip_panel.h"

namespace sbso::app {

class HelpScreen : public Screen {
public:
    // `symbol`: help_3, help_cards_contents, ... `hide_close`: only the first/last steps keep their Close button.
    HelpScreen(App& app, const std::string& symbol, bool hide_close, std::function<void(bool show_instructions)> done);
    void tick(App& app) override;
    void draw(App& app, std::vector<engine::DrawCmd>& out) override;
    void pointer_move(App& app, engine::Point p) override { panel_.pointer_move(app, p); }
    void pointer_down(App& app, engine::Point p) override { panel_.pointer_down(app, p); }
    void pointer_up(App& app, engine::Point p) override;
    void key_down(App& app, int key) override;
    bool valid() const { return panel_.valid(); }

private:
    void finish(App& app, bool instructions);
    ClipPanel panel_;
    bool hide_close_;
    std::string label_;
    std::function<void(bool)> done_;
    bool finished_ = false;
    int dim_depth_ = 0;  // depth of the full-screen dimming shape inside cContent (0: none)
};

}  // namespace sbso::app
