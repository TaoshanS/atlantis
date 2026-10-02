#include "menu_screens.h"

#include "engine/draw_util.h"
#include "engine/shapes.h"

namespace sbso::app {
using engine::Matrix;

namespace {
const engine::SolidPath& dim_path(float alpha) {
    static engine::SolidPath p;
    p.rgba[0] = p.rgba[1] = p.rgba[2] = 0;
    p.rgba[3] = alpha;
    p.contours.clear();
    p.contours.push_back({{-3000, -3000}, {3000, -3000}, {3000, 3000}, {-3000, 3000}});
    return p;
}

constexpr int kEscape = 27;
const char* const kPages[12] = {"one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve"};
}  // namespace

void draw_dim(std::vector<engine::DrawCmd>& out, float alpha) {
    engine::DrawCmd d;
    d.kind = engine::DrawCmd::Kind::Solid;
    d.solid = &dim_path(alpha);
    d.m = Matrix{};
    out.push_back(d);
}

// --- InGameMenu ------------------------------------------------------------------------------------------------

InGameMenuScreen::InGameMenuScreen(App& app, std::function<void(const std::string&)> done) : done_(std::move(done)), skin_(app) {
    const engine::Library* lib = app.assets().library("menu_assets");
    if (!lib) return;
    auto clip = engine::MovieClip::create(lib, lib->symbol_id("inGameMenu"), &app);
    engine::Rect b = clip->bounds();
    // "Saved games" joins the original buttons as a fifth one, above QUIT: the baked panel is replaced by a taller one cut from the same art
    const float step = 55.0f;
    float qx = 0, qy = 0, ex = 0, slot_y = 0;
    if (!app.profile_id().empty() && clip->get_child_xy("cExit", &qx, &qy)) {
        slot_y = qy;  // QUIT moves down one step; the new button takes its place
        clip->set_depth_hidden(1, true);
        clip->set_child_xy("cExit", qx, qy + step);
        ex = step;
    }
    extra_ = ex;
    Matrix m{1, 0, 0, 1, sbso::kDesignW / 2.0f - (b.x0 + b.x1) / 2, sbso::kDesignH / 2.0f - (b.y0 + b.y1 + ex) / 2};
    panel_ = ClipPanel(std::move(clip), m, {"cReturn", "cOptions", "cExit", "cInstructions"});
    frame_ = engine::Rect{m.tx + b.x0, m.ty + b.y0, m.tx + b.x1, m.ty + b.y1 + ex};
    if (ex > 0) saves_rect_ = engine::Rect{m.tx + qx, m.ty + slot_y, m.tx + qx + 220, m.ty + slot_y + 49};  // 49: the button body; the bitmap adds its drop shadow below
}

void InGameMenuScreen::draw(App& app, std::vector<engine::DrawCmd>& out) {
    skin_.begin();
    draw_dim(out, 0.65f);
    if (extra_ > 0) skin_.panel(out, frame_);
    // the added button goes under the original ones (their hover brackets overlap it), its own brackets above everything
    std::vector<engine::DrawCmd> brackets;
    if (saves_rect_.w() > 0) {
        bool hover = pointer_.x >= saves_rect_.x0 && pointer_.x < saves_rect_.x1 && pointer_.y >= saves_rect_.y0 && pointer_.y < saves_rect_.y1;
        skin_.button(out, saves_rect_, app.tr("profile.saves", "SAVED GAMES"), hover, true, false, 27, &brackets);
    }
    panel_.draw(out);
    out.insert(out.end(), brackets.begin(), brackets.end());
}

void InGameMenuScreen::choose(App& app, const std::string& what) {
    app.close_overlay();
    if (done_) { auto cb = std::move(done_); done_ = nullptr; cb(what); }
}

void InGameMenuScreen::pointer_up(App& app, engine::Point p) {
    std::string b = panel_.pointer_up(app, p);
    if (b.empty() && saves_rect_.w() > 0 && p.x >= saves_rect_.x0 && p.x < saves_rect_.x1 && p.y >= saves_rect_.y0 && p.y < saves_rect_.y1) b = "cSaves";
    if (b.empty()) return;
    app.sound().play_sound("button_click", false, -1);
    if (b == "cSaves") { choose(app, "saves"); return; }
    choose(app, b == "cReturn" ? "resume" : b == "cOptions" ? "options" : b == "cExit" ? "exit" : "instructions");
}

void InGameMenuScreen::key_down(App& app, int key) {
    if (key == kEscape) choose(app, "resume");
}

// --- Really quit -----------------------------------------------------------------------------------------------

ReallyQuitScreen::ReallyQuitScreen(App& app, std::string text, std::function<void(bool)> done) : done_(std::move(done)) {
    const engine::Library* lib = app.assets().library("menu_assets");
    if (!lib) return;
    auto clip = engine::MovieClip::create(lib, lib->symbol_id("really_quit"), &app);
    clip->set_text("cText", text);
    panel_ = ClipPanel(std::move(clip), Matrix{1, 0, 0, 1, sbso::kDesignW / 2.0f, sbso::kDesignH / 2.0f - 60}, {"cYesButt", "cNoButt"});
}

void ReallyQuitScreen::draw(App&, std::vector<engine::DrawCmd>& out) {
    draw_dim(out, 0.65f);
    panel_.draw(out);
}

void ReallyQuitScreen::choose(App& app, bool yes) {
    app.close_overlay();
    if (done_) { auto cb = std::move(done_); done_ = nullptr; cb(yes); }
}

void ReallyQuitScreen::pointer_up(App& app, engine::Point p) {
    std::string b = panel_.pointer_up(app, p);
    if (b.empty()) return;
    app.sound().play_sound("button_click", false, -1);
    choose(app, b == "cYesButt");
}

void ReallyQuitScreen::key_down(App& app, int key) {
    if (key == kEscape) choose(app, false);
}

// --- Instruction book ------------------------------------------------------------------------------------------

InstructionsScreen::InstructionsScreen(App& app, std::function<void()> done) : done_(std::move(done)) {
    const engine::Library* lib = app.assets().library("instructions");
    if (!lib) return;
    auto clip = engine::MovieClip::create(lib, lib->symbol_id("InstructBook"), &app);
    panel_ = ClipPanel(std::move(clip), Matrix{1, 0, 0, 1, sbso::kDesignW / 2.0f, sbso::kDesignH / 2.0f},
                       {"cNextButton", "cPrevButton", "cCloseButton", "cToolTips", "cContinue"});
    goto_page(0);
}

void InstructionsScreen::goto_page(int page) {
    if (!panel_.valid()) return;
    page_ = page < 0 ? 0 : page > 11 ? 11 : page;
    engine::MovieClip* c = panel_.clip();
    c->goto_label_and_stop(kPages[page_]);
    c->set_child_visible("cPrevButton", page_ != 0);
    c->set_child_visible("cNextButton", page_ != 11);
}

void InstructionsScreen::close(App& app) {
    app.close_overlay();
    if (done_) { auto cb = std::move(done_); done_ = nullptr; cb(); }
}

void InstructionsScreen::draw(App& app, std::vector<engine::DrawCmd>& out) {
    draw_dim(out, 0.65f);
    if (engine::MovieClip* c = panel_.clip()) {
        // wide screens: the 640x480 backdrop (depth 1) spans the whole scene, and the CONTINUE / TOOL TIPS buttons, parked off the
        // original stage (x 467 / 724), stay hidden while they are out there
        float extra = (app.layout().logical_w - sbso::kDesignW) / 2.0f + 8.0f;
        c->stretch_depth_x(1, -sbso::kDesignW / 2.0f - extra, sbso::kDesignW / 2.0f + extra);
        if (kMobile) c->enlarge_named("cCloseButton", kMobileCloseScale);
        for (const char* n : {"cContinue", "cToolTips"}) {
            float x = 0, y = 0;
            if (c->get_child_xy(n, &x, &y)) c->set_child_visible(n, x < sbso::kDesignW / 2.0f);
        }
    }
    panel_.draw(out);
}

void InstructionsScreen::pointer_up(App& app, engine::Point p) {
    std::string b = panel_.pointer_up(app, p);
    if (b.empty()) return;
    app.sound().play_sound("button_click", false, -1);
    if (b == "cNextButton") goto_page(page_ + 1);
    else if (b == "cPrevButton") goto_page(page_ - 1);
    else close(app);  // close button, tool tips and "continue" all close the widget
}

void InstructionsScreen::key_down(App& app, int key) {
    if (key == kEscape) close(app);
}

}  // namespace sbso::app
