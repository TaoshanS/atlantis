#include "profile_screen.h"

#include "app/flow.h"

#include <algorithm>
#include <cctype>
#include "engine/draw_util.h"
#include "engine/shapes.h"

#include "app/screens/saves_screen.h"
#include "app/ui/menu_screens.h"

namespace sbso::app {
using engine::Matrix;
using engine::MovieClip;
using engine::Point;

namespace {
constexpr int kMaxNameLength = 15;
constexpr int kWidgets = 3;
const float kWidgetX[kWidgets] = {-234, -79, 77};
constexpr float kWidgetY = 112;
constexpr int kBackspace = 8, kDelete = 127, kEnter = 13, kEscape = 27;
}  // namespace

ProfileScreen::ProfileScreen(App& app, std::function<void()> on_changed, std::function<void()> on_closed)
    : app_(app), skin_(app), on_changed_(std::move(on_changed)), on_closed_(std::move(on_closed)) {
    lib_ = app.assets().library("main");
    place_ = Matrix{1, 0, 0, 1, sbso::kDesignW / 2.0f - 1, sbso::kDesignH / 2.0f + 4};
    cursor_shape_.rgba[0] = cursor_shape_.rgba[1] = cursor_shape_.rgba[2] = cursor_shape_.rgba[3] = 1;
    cursor_shape_.contours.push_back({{-10, -7}, {10, -7}, {10, -5}, {-10, -5}});
    if (app.current_profile_name().empty() && app.list_profiles().empty()) {
        input_.clear();
        change_state(State::Rename);
    } else {
        change_state(State::View);
    }
}

// same row and height as the original OK button (body 190..226 below the panel centre), to its right
engine::Rect ProfileScreen::saves_button() const { return engine::Rect{place_.tx + 78, place_.ty + 190, place_.tx + 214, place_.ty + 226}; }

ProfileScreen::~ProfileScreen() { app_.start_text_input(false); }

void ProfileScreen::change_state(State s) {
    state_ = s;
    widgets_.clear();
    brackets_.reset();
    bracket_index_ = selected_ = -1;
    if (!lib_) return;
    const char* sym = s == State::View ? "ProfileScreen_profileView" : s == State::Rename ? "ProfileScreen_profileRename" : "ProfileScreen_profileDelete";
    bg_ = ClipPanel(MovieClip::create(lib_, lib_->symbol_id(sym), &app_), place_, {});
    if (s == State::View) build_view();
    else if (s == State::Rename) build_rename();
    else update_buttons();
    app_.start_text_input(s == State::Rename);
}

void ProfileScreen::build_view() {
    auto profiles = app_.list_profiles();
    int current = app_.current_profile_index();
    for (int i = 0; i < kWidgets; ++i) {
        Widget w;
        w.index = i;
        w.is_new = i >= static_cast<int>(profiles.size());
        Matrix m = place_ * Matrix{1, 0, 0, 1, kWidgetX[i], kWidgetY};
        if (w.is_new) {
            w.panel = ClipPanel(MovieClip::create_any(lib_, lib_->symbol_id("ProfileWidget_button_new"), &app_), m, {"*"});
        } else {
            auto c = MovieClip::create(lib_, lib_->symbol_id("ProfileWidget_profileWidget"), &app_);
            c->set_text("cName", profiles[i].name);
            c->set_child_alpha("cBackground", i == current ? 1.0f : 0.0f);
            w.panel = ClipPanel(std::move(c), m, {"cRename", "cDelete"});
        }
        engine::Rect b = w.panel.clip()->bounds();
        Point p0 = m.apply({b.x0, b.y0}), p1 = m.apply({b.x1, b.y1});
        w.bounds = {p0.x, p0.y, p1.x, p1.y};
        widgets_.push_back(std::move(w));
    }
    selected_ = current;
    brackets_ = MovieClip::create(lib_, lib_->symbol_id("ProfileWidgetRadioSet_brackets"), &app_);
    move_brackets(std::max(0, current), false);
    bg_.set_names({"cCloseButton"});
}

void ProfileScreen::build_rename() {
    is_new_ = input_.empty();
    bool first = is_new_ && app_.current_profile_name().empty();
    close_enabled_ = !first;
    ok_enabled_ = !first && !is_new_;
    blink_ = 0;
    cursor_visible_ = true;
    update_buttons();
    bg_.clip()->set_text("cNewName", input_);
    update_cursor();
}

void ProfileScreen::update_buttons() {
    if (state_ == State::Rename) {
        std::vector<std::string> n;
        if (close_enabled_) { n.push_back("cCloseButton"); n.push_back("cCancelButton"); }
        if (ok_enabled_) n.push_back("cOkButton");
        bg_.set_names(n);
    } else if (state_ == State::Delete) {
        bg_.set_names({"cCloseButton", "cNoButton", "cYesButton"});
    }
}

void ProfileScreen::check_uniqueness() {
    for (const auto& p : app_.list_profiles())
        if (p.name == input_) { ok_enabled_ = false; update_buttons(); return; }
    ok_enabled_ = !input_.empty();
    update_buttons();
}

void ProfileScreen::move_brackets(int index, bool sound) {
    if (bracket_index_ == index) return;
    if (sound) app_.sound().play_sound("button_rollover", false, -1);
    bracket_index_ = index;
}

void ProfileScreen::darken(int index) {
    if (index == selected_) return;
    if (selected_ >= 0 && selected_ < static_cast<int>(widgets_.size()) && !widgets_[selected_].is_new) widgets_[selected_].panel.clip()->set_child_alpha("cBackground", 0);
    selected_ = index;
    if (index >= 0 && index < static_cast<int>(widgets_.size()) && !widgets_[index].is_new) widgets_[index].panel.clip()->set_child_alpha("cBackground", 1);
}

void ProfileScreen::update_cursor() {
    MovieClip* c = bg_.clip();
    engine::Rect r;
    if (!c || !c->child_bounds("cNewName", &r)) return;
    float x = r.x0 + (r.x1 - r.x0) / 2, y = r.y1;
    if (!input_.empty()) {
        std::vector<engine::DrawCmd> cmds;
        c->collect(cmds, Matrix{}, engine::ColorTransform{});
        for (const auto& d : cmds)
            if (d.kind == engine::DrawCmd::Kind::Text && d.run.text == input_) {
                engine::TextRun probe = d.run;  // origin-anchored: the bitmap width is the text advance plus a fixed margin
                probe.align = 0;
                probe.wrap = false;
                probe.has_origin = true;
                probe.ox = 0;
                probe.oy = probe.size;
                engine::TextRaster::Result res = app_.assets().text().render(probe, 1.0f);
                float text_w = std::max(0.0f, res.w - 4 - 0.2f * probe.size);
                x += text_w / 2 + 12;
                break;
            }
        if (static_cast<int>(input_.size()) >= kMaxNameLength) y += 1000;
    }
    cursor_pos_ = {x, y};
}

void ProfileScreen::close(App& app) {
    app.close_overlay();
    if (on_closed_) on_closed_();
}

void ProfileScreen::apply_rename(App& app) {
    if (input_.empty()) return;
    if (is_new_ && !app.current_profile_name().empty()) app.create_profile(input_);
    else app.rename_current_profile(input_);
    if (on_changed_) on_changed_();
    change_state(State::View);
}

void ProfileScreen::tick(App&) {
    bg_.tick();
    for (auto& w : widgets_) w.panel.tick();
    if (brackets_) brackets_->tick();
    if (state_ == State::Rename && ++blink_ > 10) { blink_ = 0; cursor_visible_ = !cursor_visible_; }
}

bool ProfileScreen::over_saves(const App& app, Point p) const {
    engine::Rect r = saves_button();
    return state_ == State::View && !app.profile_id().empty() && p.x >= r.x0 && p.x < r.x1 && p.y >= r.y0 && p.y < r.y1;
}

void ProfileScreen::pointer_move(App& app, Point p) {
    pointer_ = p;
    if (over_saves(app, p)) p = {-1000.0f, -1000.0f};  // the added button owns the pointer: no original button may light up under it
    bg_.pointer_move(app, p);
    for (auto& w : widgets_) {
        w.panel.pointer_move(app, p);
        if (p.x >= w.bounds.x0 && p.x < w.bounds.x1 && p.y >= w.bounds.y0 && p.y < w.bounds.y1) move_brackets(w.index, true);
    }
}

void ProfileScreen::pointer_down(App& app, Point p) {
    if (over_saves(app, p)) p = {-1000.0f, -1000.0f};
    bg_.pointer_down(app, p);
    for (auto& w : widgets_) {
        w.panel.pointer_down(app, p);
        if (p.x >= w.bounds.x0 && p.x < w.bounds.x1 && p.y >= w.bounds.y0 && p.y < w.bounds.y1) {  // MOUSE_DOWN on the widget
            if (!w.is_new && app.current_profile_index() != w.index) {
                app.switch_profile(w.index);
                if (on_changed_) on_changed_();
            }
            darken(w.index);
            app.sound().play_sound("battle_card_select", false, -1);
        }
    }
}

void ProfileScreen::pointer_up(App& app, Point p) {
    const bool on_saves = over_saves(app, p);
    Point q = on_saves ? Point{-1000.0f, -1000.0f} : p;
    std::string b = bg_.pointer_up(app, q);
    std::vector<std::string> wb;
    for (auto& w : widgets_) wb.push_back(w.panel.pointer_up(app, q));
    if (on_saves) {
        app.sound().play_sound("button_click", false, -1);
        app.open_overlay(std::make_unique<SavesScreen>(app, [&app]() { flow::after_slot_loaded(app); }));
        return;
    }
    if (state_ == State::View) {
        if (b == "cCloseButton") { app.sound().play_sound("button_click", false, -1); close(app); return; }
        for (size_t i = 0; i < wb.size(); ++i) {
            if (wb[i].empty()) continue;
            app.sound().play_sound("button_click", false, -1);
            if (widgets_[i].is_new) { input_.clear(); change_state(State::Rename); }
            else if (wb[i] == "cRename") { input_ = app.current_profile_name(); change_state(State::Rename); }
            else if (wb[i] == "cDelete") { change_state(State::Delete); }
            return;
        }
    } else if (state_ == State::Rename) {
        if (b.empty()) return;
        app.sound().play_sound("button_click", false, -1);
        if (b == "cCloseButton") close(app);
        else if (b == "cOkButton") apply_rename(app);
        else if (b == "cCancelButton") change_state(State::View);
    } else if (state_ == State::Delete) {
        if (b.empty()) return;
        app.sound().play_sound("button_click", false, -1);
        if (b == "cCloseButton") close(app);
        else if (b == "cNoButton") change_state(State::View);
        else if (b == "cYesButton") {
            app.delete_current_profile();
            if (on_changed_) on_changed_();
            if (app.current_profile_name().empty()) { input_.clear(); change_state(State::Rename); }
            else change_state(State::View);
        }
    }
}

void ProfileScreen::text_input(App&, const std::string& utf8) {
    if (state_ != State::Rename) return;
    for (unsigned char ch : utf8) {
        if (static_cast<int>(input_.size()) >= kMaxNameLength) break;
        if (ch >= 32 && ch <= 126) input_ += static_cast<char>(std::toupper(ch));
    }
    bg_.clip()->set_text("cNewName", input_);
    check_uniqueness();
    update_cursor();
}

void ProfileScreen::key_down(App& app, int key) {
    if (state_ == State::Rename) {
        if (key == kBackspace || key == kDelete) {
            if (!input_.empty()) input_.pop_back();
            bg_.clip()->set_text("cNewName", input_);
            if (input_.empty()) { ok_enabled_ = false; update_buttons(); }
            else check_uniqueness();
            update_cursor();
        } else if (key == kEnter && ok_enabled_) {
            apply_rename(app);
        }
    }
    if (key == kEscape && (state_ != State::Rename || close_enabled_)) {
        if (state_ == State::View) close(app);
        else change_state(State::View);
    }
}

void ProfileScreen::draw(App&, std::vector<engine::DrawCmd>& out) {
    draw_dim(out, 0.65f);
    if (kMobile && bg_.valid()) bg_.clip()->enlarge_named("cCloseButton", kMobileCloseScale);
    bg_.draw(out);
    for (auto& w : widgets_) w.panel.draw(out);
    // the profile brackets step aside while the added button shows its own (otherwise two sets of brackets are lit at once)
    if (brackets_ && bracket_index_ >= 0 && !over_saves(app_, pointer_))
        brackets_->collect(out, place_ * Matrix{1, 0, 0, 1, kWidgetX[bracket_index_] - 5, 105}, engine::ColorTransform{});
    if (state_ == State::View && !app_.profile_id().empty()) {  // "saved games" button next to the OK button, in the original button style
        saves_rect_ = saves_button();
        bool hover = pointer_.x >= saves_rect_.x0 && pointer_.x < saves_rect_.x1 && pointer_.y >= saves_rect_.y0 && pointer_.y < saves_rect_.y1;
        skin_.begin();
        skin_.button(out, saves_rect_, app_.tr("profile.saves", "SAVED GAMES"), hover, true, false, 18);
    }
    if (state_ == State::Rename && cursor_visible_) {
        engine::DrawCmd d;
        d.kind = engine::DrawCmd::Kind::Solid;
        d.solid = &cursor_shape_;
        d.m = place_ * Matrix{1, 0, 0, 1, cursor_pos_.x, cursor_pos_.y};
        out.push_back(d);
    }
}

}  // namespace sbso::app
