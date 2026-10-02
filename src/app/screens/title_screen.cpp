#include "title_screen.h"

#include "app/flow.h"
#include "battle_screen.h"
#include "options_screen.h"
#include "profile_screen.h"
#include "app/ui/clip_screens.h"
#include "app/ui/menu_screens.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <random>

namespace sbso::app {
namespace {

constexpr int kLoopStart = 200;        // gotoAndPlay(200) when returning from the game (1-based)
constexpr int kSkipTargets[] = {85, 140, 190};  // TitleScreen.skipSplash
constexpr int kPopEnd = 139;           // last frame of "this is pop" before the Big Fish crossfade (1-based)

const char* kButtons[] = {"cPlayButt", "cProfileButt", "cOptionsButt", "cQuitButt", "cCreditsButt"};

std::string button_of(const engine::ButtonHit& h) {
    for (const char* b : kButtons)
        if (h.owner->in_named(b)) return b;
    return {};
}

}  // namespace

TitleScreen::TitleScreen(App& app, bool skip_to_menu) : skin_(app) {
    lib_ = app.assets().library("titlescreen");
    keep_big_fish_ = app.options().keep_big_fish;
    place_ = engine::Matrix{1, 0, 0, 1, sbso::kDesignW / 2.0f, sbso::kDesignH / 2.0f};
    if (!lib_) return;
    clip_ = engine::MovieClip::create(lib_, lib_->symbol_id("titlescreen"), &app);
    if (skip_to_menu) {
        clip_->goto_and_play(kLoopStart);
        phase_ = Phase::Splash;
    }
}

void TitleScreen::tick(App& app) {
    if (!clip_) return;
    clip_->tick();
    if (kMobile) clip_->set_child_visible("cQuitButt", false);  // apps do not quit themselves on iOS / Android
    switch (phase_) {
        case Phase::Preload:
            // preloadAnim: the timeline stops at frame 26 (frame script); assets are already loaded, so the loader bar fills at once.
            if (clip_->current_frame() == 25) {
                if (engine::MovieClip* bar = clip_->child("mLoaderbar")) bar->goto_and_stop(bar->total_frames());
                if (++preload_wait_ > 8) {
                    phase_ = Phase::Splash;  // getMenu()
                    clip_->goto_label_and_play("menu");
                }
            }
            break;
        case Phase::Splash:
            if (!keep_big_fish_ && clip_->current_frame() + 1 == kPopEnd) clip_->goto_and_play(kLoopStart);
            if (clip_->current_frame() + 1 == clip_->total_frames()) {  // loadedAnim
                clip_->stop();
                phase_ = Phase::Menu;
                init_buttons(app);
            }
            break;
        case Phase::Menu:
            update_hover(app);
            break;
    }
}

void TitleScreen::init_buttons(App& app) {
    clip_->set_text("cText", welcome_message(app));
    if (app.current_profile_name().empty()) open_profile_screen(app);  // first run: ask for a name
}

void TitleScreen::open_profile_screen(App& app) {
    app.open_overlay(std::make_unique<ProfileScreen>(
        app, [this, &app]() { if (clip_) clip_->set_text("cText", welcome_message(app)); },
        [&app]() { app.session().level_just_completed = false; }));
}

std::string TitleScreen::welcome_message(App& app) {
    // Profile name comes from the current profile; an unnamed profile gets the first-run message.
    std::string name = app.current_profile_name();
    if (name.empty()) return app.tr("title.welcome.first", "Well, hello! Click \"Play\" and let's get started!");
    static std::mt19937 rng{std::random_device{}()};
    static const char* kDefaults[10] = {
        "Thank Neptune you're here {name}!\nClick \"Play\"!\nFor the love of Krabby Patties, click \"Play\"!",
        "Ah it's you, {name}!\nNow we can finish our epic duel! Oh, wait,\nyou’re on my side? Then just click \"Play.\"",
        "Quick, {name}!\nThere’s no time to lose!\nClick \"Play\" right now, and let’s get to it!",
        "Give up {name}, It’s too hard!\nGive up while you have a chance!\nOh, I’m just kidding! Click \"Play\"!",
        "Onward, {name}!\nThere’s no stopping us now!\nUnless you never click \"Play\"! Let’s go!",
        "Who is it? AHHHHHH! Oh, it’s you, {name}!\nI thought you were another of Plankton's goons!\nClick \"Play\" while my pulse goes back to normal.",
        "Let’s go, {name}!\nOnce more unto the breach, dear friends!\nI don’t know what that means, but click \"Play\" anyway!",
        "Now, {name}!\nLet’s catch the enemy off guard!\nClick \"Play,\" why don’t you?",
        "Do you think this is a game {name}?\nWell, I guess it is, isn't it?\nClick \"Play,\" in that case!",
        "Phew!\nI thought you’d never show up {name}!\nLet’s go, click \"Play\"!"};
    int i = static_cast<int>(rng() % 10);
    return i18n::Strings::format(app.tr("title.welcome." + std::to_string(i), kDefaults[i]), {{"name", name}});
}

void TitleScreen::skip_splash() {
    int f = clip_->current_frame() + 1;
    if (f < kSkipTargets[0]) clip_->goto_and_play(kSkipTargets[0]);
    else if (f < kSkipTargets[1]) clip_->goto_and_play(keep_big_fish_ ? kSkipTargets[1] : kLoopStart);  // no Big Fish splash to skip into
    else if (f < kSkipTargets[2]) clip_->goto_and_play(kSkipTargets[2]);
}

void TitleScreen::update_hover(App& app) {
    std::vector<engine::ButtonHit> hits;
    clip_->collect_buttons(hits, place_);
    std::string hovered;
    const engine::ButtonHit* top = nullptr;
    for (const auto& h : hits) {
        if (!h.contains(pointer_)) continue;
        std::string n = button_of(h);
        if (!n.empty()) { hovered = n; top = &h; }  // later entries are painted on top
    }
    for (auto& h : hits) {  // final state per button, applied once (entering a state restarts its sprites)
        bool is_top = top && h.owner == top->owner && h.depth == top->depth;
        h.owner->set_button_state(h.depth, is_top ? (pointer_down_ ? engine::MovieClip::ButtonState::Down : engine::MovieClip::ButtonState::Over) : engine::MovieClip::ButtonState::Up);
    }
    if (hovered != hover_name_) {
        if (!hovered.empty()) app.sound().play_sound("button_rollover", false, -1);  // ROLL_OVER
        hover_name_ = hovered;
    }
}

void TitleScreen::handle_click(App& app, const std::string& b) {
    last_action_ = b;
    if (app.options().headless) std::fprintf(stderr, "title: click %s\n", b.c_str());
    app.sound().play_sound(b == "cPlayButt" ? "button_play" : "button_click", false, -1);
    if (b == "cPlayButt") flow::play(app);
    if (b == "cQuitButt" && !kMobile)
        app.open_overlay(std::make_unique<ReallyQuitScreen>(app, app.tr("quit.text", "Progress Saved!  Really Quit?"), [&app](bool yes) {
            if (yes) { app.autosave(); app.quit(); }
        }));
    if (b == "cProfileButt") open_profile_screen(app);
    if (b == "cCreditsButt") app.open_overlay(std::make_unique<CreditsScreen>(app, engine::Point{sbso::kDesignW / 2.0f, sbso::kDesignH / 2.0f + 30}, nullptr));
    if (b == "cOptionsButt") app.open_overlay(std::make_unique<OptionsScreen>(app));
}

void TitleScreen::pointer_move(App& app, engine::Point p) {
    pointer_ = p;
    if (phase_ == Phase::Menu && clip_) update_hover(app);
}

engine::Rect TitleScreen::quit_rect() {  // where the (hidden) QUIT button is drawn right now: the menu slides its buttons in
    engine::Rect r{}, b{};
    if (!clip_ || !clip_->child_bounds("cQuitButt", &b)) return r;
    engine::Point p0 = place_.apply({b.x0, b.y0}), p1 = place_.apply({b.x1, b.y1});
    return engine::Rect{p0.x, p0.y, p1.x, p1.y};
}

int TitleScreen::latest_slot(App& app) {
    int best = -1;
    std::int64_t when = 0;
    auto slots = app.list_slots();
    for (int i = 0; i < static_cast<int>(slots.size()); ++i)
        if (slots[i].exists && !slots[i].corrupt && (best < 0 || slots[i].modified_unix > when)) { best = i; when = slots[i].modified_unix; }
    return best;
}

void TitleScreen::load_latest(App& app) {
    int slot = latest_slot(app);
    if (slot < 0) return;
    app.sound().play_sound("button_play", false, -1);
    if (app.load_from_slot(slot)) flow::after_slot_loaded(app);  // resumes the battle snapshot, or opens the map
}

void TitleScreen::pointer_down(App& app, engine::Point p) {
    pointer_ = p;
    auto inside = [&](const engine::Rect& r) { const float m = engine::ButtonHit::slop; return p.x >= r.x0 - m && p.x < r.x1 + m && p.y >= r.y0 - m && p.y < r.y1 + m; };
    if (kMobile && phase_ == Phase::Menu) load_rect_ = quit_rect();
    load_down_ = kMobile && phase_ == Phase::Menu && load_rect_.w() > 0 && inside(load_rect_) && latest_slot(app) >= 0;
    if (load_down_) return;
    if (phase_ == Phase::Menu && clip_) update_hover(app);
    pointer_down_ = true;
    down_name_ = hover_name_;
    if (phase_ == Phase::Menu && clip_) update_hover(app);
}

void TitleScreen::pointer_up(App& app, engine::Point p) {
    pointer_ = p;
    bool was_down = pointer_down_;
    pointer_down_ = false;
    if (!clip_) return;
    if (load_down_) {
        load_down_ = false;
        auto inside = [&](const engine::Rect& r) { const float m = engine::ButtonHit::slop; return p.x >= r.x0 - m && p.x < r.x1 + m && p.y >= r.y0 - m && p.y < r.y1 + m; };
        if (inside(load_rect_)) load_latest(app);
        return;
    }
    if (phase_ == Phase::Splash && was_down) { skip_splash(); return; }
    if (phase_ == Phase::Menu) {
        update_hover(app);
        if (was_down && !down_name_.empty() && down_name_ == hover_name_) handle_click(app, down_name_);
        down_name_.clear();
    }
}

void TitleScreen::key_down(App&, int) {}

void TitleScreen::draw(App& app, std::vector<engine::DrawCmd>& out) {
    if (!clip_) return;
    if (kMobile) clip_->set_child_visible("cQuitButt", false);  // also here: the timeline re-places it between ticks (it flashed for a frame)
    clip_->collect(out, place_, engine::ColorTransform{});
    if (kMobile && phase_ == Phase::Menu) {
        load_rect_ = quit_rect();
        if (load_rect_.w() > 0) {
            skin_.begin();
            bool can = latest_slot(app) >= 0;
            // the title buttons are 126x51 bitmaps whose body is the top 48 rows (the rest is their soft shadow)
            const float body = load_rect_.h() * 48.0f / 51.0f;
            // +1.5 px: the menu_assets bitmap Skin draws from has a transparent 1 px rim the title bitmaps do not have
            skin_.button(out, engine::Rect{load_rect_.x0 - 1.5f, load_rect_.y0 - 1.0f, load_rect_.x1 + 1.5f, load_rect_.y0 + body + 0.5f}, app.tr("title.load", "LOAD"), load_down_, can, false, 30);
        }
    }
}

}  // namespace sbso::app
