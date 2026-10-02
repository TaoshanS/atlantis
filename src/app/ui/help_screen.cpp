#include "help_screen.h"

namespace sbso::app {
using engine::Matrix;
using engine::MovieClip;

HelpScreen::HelpScreen(App& app, const std::string& symbol, bool hide_close, std::function<void(bool)> done) : hide_close_(hide_close), done_(std::move(done)) {
    const engine::Library* lib = app.assets().library("help");
    if (!lib || lib->symbol_id(symbol) < 0) { finished_ = true; return; }
    panel_ = ClipPanel(MovieClip::create(lib, lib->symbol_id(symbol), &app), Matrix{1, 0, 0, 1, sbso::kDesignW / 2.0f, sbso::kDesignH / 2.0f}, {"cNext", "cClose"});
    // help_2..help_5 (the battle tutorial) dim the screen with a 640x480 black shape at depth 1 of their content
    dim_depth_ = symbol.size() == 6 && symbol.compare(0, 5, "help_") == 0 && symbol[5] >= '0' && symbol[5] <= '9' ? 1 : 0;
}

void HelpScreen::finish(App& app, bool instructions) {
    if (finished_ && !done_) return;
    finished_ = true;
    app.close_overlay();
    if (done_) { auto cb = std::move(done_); done_ = nullptr; cb(instructions); }
}

void HelpScreen::tick(App& app) {
    if (!panel_.valid()) { finish(app, false); return; }
    panel_.tick();
    MovieClip* content = panel_.clip()->child("cContent");
    if (!content) return;
    std::string label = content->current_label();
    if (label != label_) label_ = label;
    if (hide_close_ && label_ != "menuItems" && label_ != "endHelp")
        if (MovieClip* step = content->child(label_)) step->set_child_visible("cClose", false);
    if (content->current_frame() >= content->total_frames() - 1) finish(app, false);  // finalFrame
}

void HelpScreen::pointer_up(App& app, engine::Point p) {
    std::string b = panel_.pointer_up(app, p);
    MovieClip* content = panel_.clip()->child("cContent");
    if (!content) return;
    // port improvement (touch screens): a tap anywhere is NEXT while a step waits; the last steps still need their own buttons
    if (b.empty() && !content->playing() && label_ != "menuItems" && label_ != "endHelp") b = "cNext";
    if (b.empty()) return;
    app.sound().play_sound("button_click", false, -1);
    if (b == "cClose") finish(app, label_ == "endHelp");
    else if (b == "cNext") content->play();
}

void HelpScreen::key_down(App& app, int key) {
    if (key == 27 && !hide_close_) finish(app, false);
}

void HelpScreen::draw(App& app, std::vector<engine::DrawCmd>& out) {
    if (dim_depth_ && panel_.valid())  // wide screens: the dimming covers the whole extended scene, not only the 640 px stage
        if (MovieClip* content = panel_.clip()->child("cContent")) {
            float extra = (app.layout().logical_w - sbso::kDesignW) / 2.0f + 8.0f;
            content->stretch_depth_x(dim_depth_, -sbso::kDesignW / 2.0f - extra, sbso::kDesignW / 2.0f + extra);
        }
    if (kMobile && panel_.valid()) { panel_.clip()->enlarge_named("cClose", kMobileCloseScale); panel_.clip()->enlarge_named("cNext", 1.3f); }
    panel_.draw(out);
}

}  // namespace sbso::app
