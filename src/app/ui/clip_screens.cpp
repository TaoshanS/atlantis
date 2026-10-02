#include "clip_screens.h"

#include "app/ui/menu_screens.h"

namespace sbso::app {
using engine::Matrix;
using engine::MovieClip;

CreditsScreen::CreditsScreen(App& app, engine::Point place, std::function<void()> done) : done_(std::move(done)) {
    if (const engine::Library* lib = app.assets().library("credits"))
        panel_ = ClipPanel(MovieClip::create(lib, lib->symbol_id("creditScreen"), &app), Matrix{1, 0, 0, 1, place.x, place.y}, {"cBackButton"});
    if (!panel_.valid()) finished_ = true;
}

void CreditsScreen::close_up() {
    if (closing_ || !panel_.valid()) return;
    closing_ = true;
    panel_.clip()->goto_label_and_play("close");  // playOut
}

void CreditsScreen::tick(App& app) {
    if (!panel_.valid()) { if (done_) { auto cb = std::move(done_); done_ = nullptr; app.close_overlay(); cb(); } return; }
    panel_.tick();
    MovieClip* c = panel_.clip();
    if (closing_ && c->current_frame() + 1 >= c->total_frames() && !finished_) {  // CREDITS_DONE
        finished_ = true;
        app.close_overlay();
        if (done_) { auto cb = std::move(done_); done_ = nullptr; cb(); }
    }
}

void CreditsScreen::pointer_up(App& app, engine::Point p) {
    if (closing_) return;
    if (!panel_.pointer_up(app, p).empty()) { app.sound().play_sound("button_click", false, -1); close_up(); }
}

void CreditsScreen::key_down(App&, int key) {
    if (key == 27) close_up();
}

void CreditsScreen::draw(App&, std::vector<engine::DrawCmd>& out) {
    draw_dim(out, 0.65f);
    panel_.draw(out);
}

// ------------------------------------------------------------------------------------------------ intro

IntroScreen::IntroScreen(App& app, std::function<void()> done) : done_(std::move(done)) {
    if (const engine::Library* lib = app.assets().library("SBintro2"))
        panel_ = ClipPanel(MovieClip::create(lib, lib->symbol_id("IntroMovie"), &app), Matrix{1, 0, 0, 1, sbso::kDesignW / 2.0f, sbso::kDesignH / 2.0f}, {"cArrowButt", "cGoButt"});
    if (!panel_.valid()) finished_ = true;
}

void IntroScreen::tick(App& app) {
    if (!panel_.valid() || finished_) { if (!done_called_ && done_) { done_called_ = true; done_(); } return; }
    panel_.tick();
    MovieClip* c = panel_.clip();
    if (c->current_frame() + 1 >= c->total_frames()) {  // addFrameScript(totalFrames - 1, endIntro)
        finished_ = true;
        if (done_) { done_called_ = true; auto cb = std::move(done_); done_ = nullptr; cb(); }
    }
    (void)app;
}

void IntroScreen::pointer_up(App& app, engine::Point p) {
    // The Next / Go buttons live inside nested clips whose timeline scripts resume playback (doArrowButt / doGoButt).
    std::vector<engine::ButtonHit> hits;
    panel_.clip()->collect_buttons(hits, panel_.place());
    std::string b = panel_.pointer_up(app, p);
    if (b.empty()) return;
    app.sound().play_sound("button_click", false, -1);
    for (auto& h : hits)
        if (h.contains(p) && h.owner) {
            h.owner->play();
            if (b == "cGoButt") panel_.clip()->play();
            break;
        }
}

void IntroScreen::draw(App&, std::vector<engine::DrawCmd>& out) { panel_.draw(out); }

}  // namespace sbso::app
