#include "level_select_screen.h"

#include "app/flow.h"
#include "app/screens/options_screen.h"
#include "app/ui/menu_screens.h"

namespace sbso::app {
using engine::Matrix;
using engine::MovieClip;
using engine::Point;

LevelSelectScreen::LevelSelectScreen(App& app) : app_(app) {
    gui_ = app.assets().library("LevelSelectGUI");
    place_ = Matrix{1, 0, 0, 1, sbso::kDesignW / 2.0f, sbso::kDesignH / 2.0f};
    Session& ss = app.session();
    const save::Progress& pr = app.progress();
    chest_available_ = (ss.level_just_completed && ss.level == 1 && ss.stage == 1) || pr.unlocked_stages[0][0] == save::kCleared;
    if (gui_) {
        auto clip = MovieClip::create(gui_, gui_->symbol_id(chest_available_ ? "LevelSelectGUI.mapScreen" : "LevelSelectGUI.mapScreenAlt"), &app);
        bg_ = ClipPanel(std::move(clip), place_, {"cPlayButt", "cLeftButton", "cRightButton", "cChestButt"});
    }
    showing_ = std::max(1, std::min(9, ss.stage));
    if (const engine::Library* tb = app.assets().library("tourbus")) {
        all_levels_ = MovieClip::create(tb, tb->symbol_id("tourbus.map_all_levels"), &app);
        scrollers_ = MovieClip::create(tb, tb->symbol_id("tourbus.scrollers"), &app);
    }
    if (const engine::Library* ml = app.assets().library("menu_assets")) {
        menu_buttons_ = ClipPanel(MovieClip::create(ml, ml->symbol_id("inGameMenuButtons"), &app), Matrix{}, {"cMenuButt"});
        for (const char* n : {"cHelp", "cEyeOpen", "cEyeClosed"}) menu_buttons_.clip()->set_child_visible(n, false);  // ShowOnlyMenuButton
        engine::Rect b = menu_buttons_.clip()->bounds();
        menu_buttons_.set_place(Matrix{1, 0, 0, 1, sbso::kDesignW - 3 - b.x1, 3 - b.y0});
    }
}

LevelSelectScreen::~LevelSelectScreen() {
    if (attached_ && all_levels_) {
        if (MovieClip* place = all_levels_->child("cMapPlacement")) place->remove_child_clip(attached_->clip());
    }
    for (auto& m : maps_)
        if (m) m->recycle();
}

void LevelSelectScreen::load() {  // LevelSelectScreen.load: waits for the placeholders of the background
    MovieClip* bg = bg_.clip();
    if (!bg || !all_levels_ || !scrollers_) return;
    MovieClip* maps_place = bg->find("cMapMask.cMapAllLevelsPlacement");
    MovieClip* scroll_place = bg->child("cScrollerPlacement");
    if (!maps_place || !scroll_place) return;
    scroll_place->attach_child_clip(scrollers_.get());
    maps_place->attach_child_clip(all_levels_.get());
    loaded_ = true;
    handle_scroll(showing_);
}

void LevelSelectScreen::get_new_map(int stage) {  // LevelSelectScreenAllLevels.getNewMap
    if (attached_) {
        if (MovieClip* place = all_levels_->child("cMapPlacement")) place->remove_child_clip(attached_->clip());
        attached_ = nullptr;
    }
    MapFlag::unselect();
    all_levels_->goto_and_stop(stage);
    play_enabled_ = app_.progress().unlocked_stages[stage - 1][0] != save::kLocked;  // checkPlayButton
    if (!maps_[stage - 1]) maps_[stage - 1] = std::make_unique<StageMap>(app_, stage, this);
    else maps_[stage - 1]->check_flag_and_bus_states();
    attached_ = maps_[stage - 1].get();
    if (MovieClip* place = all_levels_->child("cMapPlacement")) place->attach_child_clip(attached_->clip());
}

void LevelSelectScreen::check_to_hide_arrows() {
    MovieClip* bg = bg_.clip();
    if (!bg) return;
    bg->set_child_visible("cLeftButton", showing_ != 1);
    bg->set_child_visible("cRightButton", showing_ != 9);
}

void LevelSelectScreen::handle_scroll(int stage) {
    enable(false);
    if (current_map()) current_map()->silence_bus();  // switching maps mid-drive: the bus loop would play forever
    if (scroll_sound_ >= 0) app_.sound().stop_sound(scroll_sound_);
    get_new_map(stage);
    showing_ = stage;
    check_to_hide_arrows();
    scrolling_ = true;
    scroll_sound_ = app_.sound().play_sound("mapscreen_map_unfold", true, -1);
    scrollers_->goto_label_and_play(scroll_start_label_);
}

void LevelSelectScreen::show_map(int delta) {
    int s = showing_ + delta;
    if (s > 0 && s < 10) handle_scroll(s);
}

void LevelSelectScreen::scroll_done() {  // SCROLL_DONE
    scrolling_ = false;
    scroll_start_label_ = "repeat";
    scrollers_->stop();
    app_.sound().stop_sound(scroll_sound_);
    if (MovieClip* mask = bg_.clip()->child("cMapMask")) mask->goto_and_stop(scrollers_->current_frame() + 1);
    // The unlock sequence belongs to the world that was just played; browsing another map must never run it there
    if (app_.session().level_just_completed && showing_ == app_.session().stage) {
        wait_unlock_ = true;
        current_map()->show_level_complete_ani();
    } else {
        enable(true);
    }
}

void LevelSelectScreen::unlock_ani_done() {
    if (wait_unlock_) {
        wait_unlock_ = false;
        enable(true);
    }
}

void LevelSelectScreen::stage_complete() {
    Session& ss = app_.session();
    if (ss.stage < 9) {
        ss.level_just_completed = false;
        ++ss.stage;
        showing_ = ss.stage;
        ss.level = 1;
        show_map(0);
    } else {
        flow::start_level(app_);  // dispatchEvent(CREATE_GAMELEVEL): stage 10 is the game-complete screen
    }
}

void LevelSelectScreen::check_complete_all_stages() {
    Session& ss = app_.session();
    if (ss.level == 6 && ss.stage == 9 && app_.progress().unlocked_stages[8][5] == save::kCleared) {
        ss.level_just_completed = false;
        complete_wait_ = true;
        complete_timer_ = 0;
    }
}

void LevelSelectScreen::handle_play() {
    Session& ss = app_.session();
    StageMap* m = current_map();
    if (!m->current_flag_is_minigame()) {
        ss.stage = m->stage();
        ss.level = m->the_level();
    }
    // A mid-level save of this very level: offer to pick the battle up where it was left.
    const save::SnapshotBlob* snap = app_.battle_snapshot();
    if (!m->current_flag_is_minigame() && ss.stage < 10 && snap && snap->stage == ss.stage && snap->level == ss.level) {
        app_.open_overlay(std::make_unique<ReallyQuitScreen>(app_, app_.tr("resume.text", "Continue your saved battle?"), [this](bool yes) {
            if (!yes) app_.clear_battle_snapshot();
            flow::battle(app_, yes);
        }));
        return;
    }
    flow::start_level(app_);
}

void LevelSelectScreen::handle_clicked(const std::string& b) {
    if (!enabled_) return;
    Session& ss = app_.session();
    StageMap* m = current_map();
    if (b == "cChestButt") {
        if (!m->current_flag_is_minigame()) {
            ss.stage = m->stage();
            ss.level = m->the_level();
        }
        app_.sound().play_sound("mapscreen_open_chest", false, -1);
        enable(false);
        flow::chest(app_);
        return;
    }
    if (b == "cPlayButt") {
        if (!play_enabled_) return;
        app_.sound().play_sound("button_play", false, -1);
        enable(false);
        handle_play();
        return;
    }
    if (b == "cLeftButton") { app_.sound().play_sound("button_click", false, -1); show_map(-1); }
    else if (b == "cRightButton") { app_.sound().play_sound("button_click", false, -1); show_map(1); }
    enable(false);
}

void LevelSelectScreen::press(const std::string& button) {
    if (intro_done_) handle_clicked(button);
}

Point LevelSelectScreen::to_map(Point p) const {
    MovieClip* bg = bg_.clip();
    Matrix a, b;
    if (!bg || !all_levels_ || !bg->path_matrix("cMapMask.cMapAllLevelsPlacement", &a) || !all_levels_->path_matrix("cMapPlacement", &b)) return {-9999, -9999};
    Matrix m = place_ * a * b;
    return m.invertible() ? m.inverse().apply(p) : Point{-9999, -9999};
}

void LevelSelectScreen::open_menu() {
    app_.sound().play_sound("mapscreen_click_menu_button", false, -1);
    app_.open_overlay(std::make_unique<InGameMenuScreen>(app_, [this](const std::string& w) { menu_choice(w); }));
}

void LevelSelectScreen::menu_choice(const std::string& what) {
    if (what == "exit") flow::title(app_, true);
    else if (what == "options") app_.open_overlay(std::make_unique<OptionsScreen>(app_));
    else if (what == "saves") flow::saved_games(app_);
    else if (what == "instructions") app_.open_overlay(std::make_unique<InstructionsScreen>(app_, nullptr));
}

float LevelSelectScreen::tick_rate() const {
    double f = app_.settings().map_fps;
    return f >= 5.0 && f <= 120.0 ? static_cast<float>(f) : 38.0f;
}

void LevelSelectScreen::tick(App&) {
    if (!bg_.valid()) return;
    if (!loaded_) load();
    MovieClip* bg = bg_.clip();
    if (!intro_done_) {
        check_to_hide_arrows();
        // monitorFlagIntro: the chest button slides in at frame 8 (only once the first level has been cleared)
        if (!chest_added_ && bg->current_frame() + 1 >= 8 && chest_available_) {
            chest_added_ = true;
            if (app_.session().level_just_completed && app_.session().level == 1 && app_.session().stage == 1) app_.sound().play_sound("sb_chest_button_reveal", false, -1);
            if (const engine::Library* lib = gui_) {
                if (MovieClip* holder = bg->child("cChestPlaceholder"))
                    holder->add_child_clip(MovieClip::create(lib, lib->symbol_id("LevelSelectGUI.chestButtonIntro"), &app_));
            }
        }
        if (bg->current_frame() + 1 >= bg->total_frames()) {  // monitorIntro: buttons go live, the clip stops
            bg->stop();
            intro_done_ = true;
            for (const char* n : {"cPlayButt", "cLeftButton", "cRightButton"}) bg->stop_button_over_clips_at_end(n);  // monitorIntro: addFrameScript(stop)
        }
    }
    bg_.tick();
    if (scrolling_) {
        if (MovieClip* mask = bg->child("cMapMask")) mask->goto_and_stop(scrollers_->current_frame() + 1);  // monitorScroll
        if (scrollers_->current_label() != scroll_start_label_) scroll_done();
    }
    if (attached_) attached_->tick();
    menu_buttons_.tick();
    if (complete_wait_ && ++complete_timer_ > 60) {  // onGameCompleteUpdate
        complete_wait_ = false;
        app_.session().stage = 10;
        stage_complete();
    }
}

void LevelSelectScreen::pointer_move(App& app, Point p) {
    menu_buttons_.pointer_move(app, p);
    if (!enabled_ || !bg_.valid()) return;
    if (intro_done_) bg_.pointer_move(app, p);
    if (attached_) attached_->pointer_move(to_map(p));
}

void LevelSelectScreen::pointer_down(App& app, Point p) {
    menu_buttons_.pointer_down(app, p);
    if (!enabled_ || !bg_.valid() || !intro_done_) return;
    bg_.pointer_down(app, p);
}

void LevelSelectScreen::pointer_up(App& app, Point p) {
    if (menu_buttons_.pointer_up(app, p) == "cMenuButt") { open_menu(); return; }
    if (!enabled_ || !bg_.valid()) return;
    if (intro_done_) {
        std::string b = bg_.pointer_up(app, p);
        if (!b.empty()) { handle_clicked(b); return; }
    }
    if (attached_) attached_->pointer_click(to_map(p));
}

void LevelSelectScreen::key_down(App&, int key) {
    if (key == 27) open_menu();
}

void LevelSelectScreen::draw(App&, std::vector<engine::DrawCmd>& out) {
    bg_.draw(out);
    menu_buttons_.draw(out);
}

}  // namespace sbso::app
