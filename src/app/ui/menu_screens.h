// Plain button overlays built from original clips: InGameMenu.as (return/options/instructions/exit), the "Really Quit?"
// confirmation (really_quit) and PageBook.as (the instruction book).
#pragma once
#include <functional>
#include <string>

#include "app/app.h"
#include "app/ui/clip_panel.h"
#include "app/ui/skin.h"

namespace sbso::app {

// Dimmed full-screen backdrop (the original draws a 0.65 alpha black rectangle).
void draw_dim(std::vector<engine::DrawCmd>& out, float alpha);

// Result is one of "resume", "options", "exit", "instructions"; the overlay is already closed when `done` runs.
class InGameMenuScreen : public Screen {
public:
    InGameMenuScreen(App& app, std::function<void(const std::string&)> done);
    void tick(App& app) override { panel_.tick(); }
    void draw(App& app, std::vector<engine::DrawCmd>& out) override;
    void pointer_move(App& app, engine::Point p) override { pointer_ = p; panel_.pointer_move(app, p); }
    void pointer_down(App& app, engine::Point p) override { panel_.pointer_down(app, p); }
    void pointer_up(App& app, engine::Point p) override;
    void key_down(App& app, int key) override;

private:
    void choose(App& app, const std::string& what);
    ClipPanel panel_;
    std::function<void(const std::string&)> done_;
    engine::Rect saves_rect_{};          // "saved games" button, fifth in the panel (done("saves")); empty without a profile
    engine::Rect frame_{};               // outer rectangle of the (taller) panel
    float extra_ = 0;                    // extra panel height for the added button
    Skin skin_;
    engine::Point pointer_{-1000, -1000};
};

// "Progress Saved! Really Quit?": done(true) when YES is chosen.
class ReallyQuitScreen : public Screen {
public:
    ReallyQuitScreen(App& app, std::string text, std::function<void(bool)> done);
    void tick(App& app) override { panel_.tick(); }
    void draw(App& app, std::vector<engine::DrawCmd>& out) override;
    void pointer_move(App& app, engine::Point p) override { panel_.pointer_move(app, p); }
    void pointer_down(App& app, engine::Point p) override { panel_.pointer_down(app, p); }
    void pointer_up(App& app, engine::Point p) override;
    void key_down(App& app, int key) override;

private:
    void choose(App& app, bool yes);
    ClipPanel panel_;
    std::function<void(bool)> done_;
};

// The 12-page illustrated instruction book (instructions.swf / InstructBook).
class InstructionsScreen : public Screen {
public:
    InstructionsScreen(App& app, std::function<void()> done);
    void tick(App& app) override { panel_.tick(); }
    void draw(App& app, std::vector<engine::DrawCmd>& out) override;
    void pointer_move(App& app, engine::Point p) override { panel_.pointer_move(app, p); }
    void pointer_down(App& app, engine::Point p) override { panel_.pointer_down(app, p); }
    void pointer_up(App& app, engine::Point p) override;
    void key_down(App& app, int key) override;
    int page() const { return page_; }

private:
    void goto_page(int page);
    void close(App& app);
    ClipPanel panel_;
    std::function<void()> done_;
    int page_ = 0;
};

}  // namespace sbso::app
