// Plain clip based screens: the credits (credits.swf creditScreen) and the first-run intro movie (SBintro2.swf IntroMovie).
#pragma once
#include <functional>
#include <memory>

#include "app/app.h"
#include "app/ui/clip_panel.h"

namespace sbso::app {

class CreditsScreen : public Screen {
public:
    // place: where the clip origin lands on screen (the title uses y+30, the ending y+15).
    CreditsScreen(App& app, engine::Point place, std::function<void()> done);
    void tick(App& app) override;
    void draw(App& app, std::vector<engine::DrawCmd>& out) override;
    void pointer_move(App& app, engine::Point p) override { panel_.pointer_move(app, p); }
    void pointer_down(App& app, engine::Point p) override { panel_.pointer_down(app, p); }
    void pointer_up(App& app, engine::Point p) override;
    void key_down(App& app, int key) override;

private:
    void close_up();
    ClipPanel panel_;
    std::function<void()> done_;
    bool closing_ = false, finished_ = false;
};

// The intro is a whole screen (not an overlay): done() runs when the movie reaches its last frame.
class IntroScreen : public Screen {
public:
    IntroScreen(App& app, std::function<void()> done);
    void tick(App& app) override;
    void draw(App& app, std::vector<engine::DrawCmd>& out) override;
    void pointer_move(App& app, engine::Point p) override { panel_.pointer_move(app, p); }
    void pointer_down(App& app, engine::Point p) override { panel_.pointer_down(app, p); }
    void pointer_up(App& app, engine::Point p) override;
    float tick_rate() const override { return 38.0f; }

private:
    ClipPanel panel_;
    std::function<void()> done_;
    bool finished_ = false, done_called_ = false;
};

}  // namespace sbso::app
