#include "scene_screens.h"

#include <algorithm>
#include <cmath>

#include "app/flow.h"
#include "app/ui/clip_screens.h"
#include "app/ui/dialogue_screen.h"
#include "app/ui/menu_screens.h"
#include "app/ui/minigame_screen.h"
#include "engine/draw_util.h"

namespace sbso::app {
using engine::LevelView;
using engine::Matrix;
using engine::MovieClip;
using sbso::game::Pt;

namespace {

constexpr int kTile = engine::kTile;
const char* kTileSwf[11] = {"", "cliffs", "palace", "atlantis_park", "suburbs", "bikini_bottom", "planktons_den", "kelp_forest", "caves", "pirateShip", "ship_graveyard"};

LevelView::UnitVisual make_actor(int id, const std::string& type, int tx, int ty) {
    LevelView::UnitVisual v;
    v.id = id;
    v.type = type;
    v.x = static_cast<float>(tx * kTile + kTile / 2);
    v.y = static_cast<float>(ty * kTile + kTile / 2);
    return v;
}

}  // namespace

// ------------------------------------------------------------------------------------------------ Krusty Krab

KrustyCutScene::KrustyCutScene(App& app, std::function<void()> done) : app_(app), done_(std::move(done)) {
    std::string err;
    loaded_ = view_.load(app.assets().extracted(), "palace", 2, &err);  // only the unit library is needed
    if (const engine::Library* lib = app.assets().library("in_game_assets"))
        bg_ = MovieClip::create(lib, lib->symbol_id("krusty_krab_interior"), &app);
    actors_.push_back(make_actor(0, "patrick", 9, 6));
    actors_.push_back(make_actor(1, "spongebob", 10, 5));
    actors_.push_back(make_actor(2, "mr_krabs", 12, 5));
    if (!loaded_ || !bg_) phase_ = Phase::Done;
}

void KrustyCutScene::tick(App& app) {
    if (phase_ == Phase::Done) {
        if (done_) { auto cb = std::move(done_); done_ = nullptr; app.close_overlay(); cb(); }
        return;
    }
    view_.tick_units();
    if (reveal_) {
        reveal_->tick();
        if (reveal_->current_frame() == 2) app.sound().play_sound("sb_chest_button_reveal", false, -1);
        if (reveal_->current_frame() >= 24) reveal_->stop();
    }
    for (auto& b : bubbles_) b->tick();
    switch (phase_) {
        case Phase::Wait:
            if (timer_++ >= 15) {
                phase_ = Phase::Dialogue;
                const auto& d = app.dialogues().krusty_krab;
                auto ds = std::make_unique<DialogueScreen>(app, d, DialogueScreen::Kind::Plain, "", 0, [this]() {
                    // fadeOut(): every character leaves in a puff of bubbles
                    if (const engine::Library* b = app_.assets().library("bubbles"))
                        for (auto& a : actors_) {
                            auto fx = MovieClip::create(b, b->symbol_id("unit_defeat_Bubbles"), &app_);
                            if (fx) bubbles_.push_back(std::move(fx));
                            a.visible = false;
                        }
                    phase_ = Phase::Fade;
                });
                ds->set_on_next([this](int shown) {
                    if (shown == 2 && !reveal_) {  // DIALOGUE_NEXT with page 2: Mr. Krabs opens the chest
                        if (const engine::Library* l = app_.assets().library("chest_reveal")) reveal_ = MovieClip::create(l, l->symbol_id("chest_reveal_mc"), &app_);
                    }
                });
                if (app.options().skip_dialogues || d.empty()) { phase_ = Phase::Fade; for (auto& a : actors_) a.visible = false; }
                else app.open_overlay(std::move(ds));
            }
            break;
        case Phase::Dialogue:
            break;
        case Phase::Fade:
            alpha_ -= alpha_ * 0.1f;  // doFade: ease to 0
            if (alpha_ < 0.05f) { alpha_ = 0; phase_ = Phase::Done; }
            break;
        case Phase::Done:
            break;
    }
}

void KrustyCutScene::draw(App&, std::vector<engine::DrawCmd>& out) {
    if (phase_ == Phase::Done) return;
    draw_dim(out, 0.65f * alpha_);
    engine::ColorTransform cx;
    cx.mult[3] = alpha_;
    if (bg_) bg_->collect(out, Matrix{}, cx);
    std::vector<engine::DrawCmd> units;
    view_.build_units(actors_, units);
    for (auto& d : units) d.cx = cx.concat(d.cx);
    out.insert(out.end(), units.begin(), units.end());
    if (reveal_) {
        const auto& a = actors_[2];
        reveal_->collect(out, Matrix{1, 0, 0, 1, a.x, a.y}, cx);
    }
    for (size_t i = 0; i < bubbles_.size() && i < actors_.size(); ++i)
        bubbles_[i]->collect(out, Matrix{1, 0, 0, 1, actors_[i].x, actors_[i].y}, cx);
}

// ------------------------------------------------------------------------------------------------ scene runner

SceneScreen::SceneScreen(App& app, Mode mode, int game) : app_(app), mode_(mode), game_(game) {
    const auto& cfg = app.config();
    std::string map_file = "palace_you_win.xml", script = "game_complete.xml";
    if (mode == Mode::Minigame) {
        if (game < 0 || game >= static_cast<int>(cfg.minigames.size())) return;
        map_file = cfg.minigames[game].map;
        script = cfg.minigames[game].anim;
    }
    std::string err;
    if (!sbso::game::load_level(app.assets().maps_dir() + "/" + map_file, &level_, &err)) { std::fprintf(stderr, "scene: %s\n", err.c_str()); return; }
    if (!sbso::game::load_cutscene(app.assets().maps_dir(), script, &steps_, &err)) { std::fprintf(stderr, "scene: %s\n", err.c_str()); return; }
    if (level_.tileset < 1 || level_.tileset > 10 || !view_.load(app.assets().extracted(), kTileSwf[level_.tileset], level_.tileset, &err)) { std::fprintf(stderr, "scene: tiles: %s\n", err.c_str()); return; }
    board_ = sbso::game::Board(level_.width, level_.height, level_.collision);
    view_.set_flag_library(app.assets().library("tourbus"));
    actors_.push_back(make_actor(next_id_++, "spongebob", level_.sb_x, level_.sb_y));
    for (const auto& sp : level_.spawns) actors_.push_back(make_actor(next_id_++, sp.name, sp.x, sp.y));
    // Map.centerMapOnPoint on SpongeBob
    float w = static_cast<float>(level_.width * kTile), h = static_cast<float>(level_.height * kTile);
    map_x_ = std::min(0.0f, std::max(sbso::kDesignW - w, sbso::kDesignW / 2.0f - (level_.sb_x * kTile + kTile / 2)));
    map_y_ = std::min(0.0f, std::max(sbso::kDesignH - h, sbso::kDesignH / 2.0f - (level_.sb_y * kTile + kTile / 2)));
    alpha_ = 1;  // GameCompleteLevel starts covered by the fade mask
    ok_ = true;
    next_step();
}

LevelView::UnitVisual* SceneScreen::actor_at(int tx, int ty) {
    for (auto& a : actors_)
        if (a.visible && static_cast<int>(a.x) / kTile == tx && static_cast<int>(a.y) / kTile == ty) return &a;
    return nullptr;
}

void SceneScreen::next_step() {  // introAnimationDone
    walks_.clear();
    while (true) {
        if (step_ >= steps_.size()) { finish(); return; }
        const auto& s = steps_[step_++];
        wait_ = Wait::None;
        do_step(s);
        if (wait_ != Wait::None) return;
        if (mode_ == Mode::GameComplete && step_ > steps_.size()) return;
    }
}

void SceneScreen::do_step(const sbso::game::CutStep& s) {
    const std::string& t = s.type;
    if (t == "dialogue" || (t == "choice" && app_.progress().minigame_plays[std::max(0, std::min(3, game_))] < 1)) {
        const auto& d = t == "dialogue" ? s.dialogue : (app_.progress().minigame_plays[game_] == 2 ? s.shortcut : s.dialogue);
        if (d.empty() || app_.options().skip_dialogues) return;
        wait_ = Wait::Dialogue;
        app_.open_overlay(std::make_unique<DialogueScreen>(app_, d, DialogueScreen::Kind::Plain, "", 0, [this]() { next_step(); }));
    } else if (t == "move") {
        LevelView::UnitVisual* a = actor_at(s.from_x, s.from_y);
        if (!a) {
            actors_.push_back(make_actor(next_id_++, s.actor, s.from_x, s.from_y));
            a = &actors_.back();
        }
        if (s.from_x != s.to_x || s.from_y != s.to_y) {
            auto path = board_.find_path({s.from_x, s.from_y}, {s.to_x, s.to_y});
            if (path.empty()) path.push_back({s.to_x, s.to_y});
            walks_.push_back({a->id, std::move(path), 0});
            wait_ = Wait::Actors;
        }
    } else if (t == "remove") {
        if (LevelView::UnitVisual* a = actor_at(s.from_x, s.from_y)) {
            a->visible = false;
            timer_ = 45;
            wait_ = Wait::Timer;
        }
    } else if (t == "cutscene") {
        // setupCutScene: the map and its units give way to the Krusty Krab interior
        actors_.clear();
        interior_ = true;
        if (const engine::Library* lib = app_.assets().library("in_game_assets")) bg_ = MovieClip::create(lib, lib->symbol_id("krusty_krab_interior"), &app_);
    } else if (t == "fadein") {
        alpha_ = 1;
        wait_ = Wait::FadeIn;
    } else if (t == "fadeout") {
        alpha_ = 0;
        wait_ = Wait::FadeOut;
    } else if (t == "minigame") {
        wait_ = Wait::Minigame;
        app_.open_overlay(std::make_unique<MinigameScreen>(app_, game_, [this]() { next_step(); }));
    }
}

void SceneScreen::finish() {
    if (mode_ == Mode::Minigame) flow::level_select(app_);
    else {  // COMPLETE_LEVEL_COMPLETE: back to the title with the last level selected
        app_.session().stage = 9;
        app_.session().level = 6;
        flow::title(app_, true);
    }
}

void SceneScreen::tick(App& app) {
    if (!ok_) { flow::level_select(app); return; }
    view_.tick_units();
    for (auto& a : actors_) {
        (void)a;
    }
    switch (wait_) {
        case Wait::Actors: {
            bool all_done = true;
            for (auto& w : walks_) {
                auto it = std::find_if(actors_.begin(), actors_.end(), [&](const LevelView::UnitVisual& u) { return u.id == w.id; });
                if (it == actors_.end()) continue;
                if (w.next >= w.path.size()) { it->label = "ready"; continue; }
                all_done = false;
                const Pt& t = w.path[w.next];
                float tx = static_cast<float>(t.x * kTile + kTile / 2), ty = static_cast<float>(t.y * kTile + kTile / 2);
                it->label = tx < it->x ? "move_left" : "move_right";
                it->x += std::max(-5.0f, std::min(5.0f, tx - it->x));
                it->y += std::max(-5.0f, std::min(5.0f, ty - it->y));
                if (it->x == tx && it->y == ty) ++w.next;
            }
            if (all_done) next_step();
            break;
        }
        case Wait::Timer:
            if (--timer_ < 0) next_step();
            break;
        case Wait::FadeIn:
            if (alpha_ > 0) alpha_ = std::max(0.0f, alpha_ - 0.05f);
            else next_step();
            break;
        case Wait::FadeOut:
            if (alpha_ < 1) alpha_ = std::min(1.0f, alpha_ + 0.05f);
            else if (mode_ == Mode::Minigame) finish();
            else {  // rollCredits: after the fade the credits roll, and when they are done the ending is over
                wait_ = Wait::Credits;
                app.switch_music("throne_room_music");
                app.open_overlay(std::make_unique<CreditsScreen>(app, engine::Point{sbso::kDesignW / 2.0f, sbso::kDesignH / 2.0f + 15}, [this]() {
                    app_.switch_music("");
                    finish();
                }));
            }
            break;
        case Wait::None:
        case Wait::Dialogue:
        case Wait::Credits:
        case Wait::Minigame:
            break;
    }
}

void SceneScreen::draw(App& app, std::vector<engine::DrawCmd>& out) {
    if (!ok_) return;
    size_t start = out.size();
    if (interior_) {
        if (bg_) bg_->collect(out, Matrix{}, engine::ColorTransform{});
        view_.build_units(actors_, out);
    } else {
        view_.build_tiles(level_, out);
        view_.build_units(actors_, out);
        engine::translate_all(out, start, map_x_, map_y_);
    }
    (void)app;
    if (alpha_ > 0) draw_dim(out, alpha_);
}

}  // namespace sbso::app
