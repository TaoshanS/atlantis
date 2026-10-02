#include "battle_screen.h"
#include "app/flow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/screens/options_screen.h"
#include "app/screens/scene_screens.h"
#include "app/ui/gauge_screen.h"
#include "app/ui/help_screen.h"
#include "app/ui/menu_screens.h"
#include "engine/draw_util.h"
#include "app/screens/title_screen.h"

namespace sbso::app {
using engine::Matrix;
using engine::Point;
using sbso::game::CardType;
using sbso::game::Pt;

namespace {

constexpr int kTile = engine::kTile;

const char* kTypeNames[4] = {"MOVE", "ATTACK", "DEFEND", "NICK"};

}  // namespace

BattleScreen::BattleScreen(App& app, int stage, int level, std::uint64_t seed, const std::vector<std::uint8_t>* resume) : app_(app), stage_(stage), level_num_(level) {
    const auto& cfg = app.config();
    if (stage < 1 || stage > static_cast<int>(cfg.stages.size())) return;
    std::string err;
    if (!sbso::game::load_level(app.assets().maps_dir() + "/" + cfg.level_file(stage, level), &level_, &err)) {
        std::fprintf(stderr, "battle: %s\n", err.c_str());
        return;
    }
    const save::Progress& pr = app.progress();
    sbso::game::SimSetup setup;
    setup.stage = stage;
    setup.level = level;
    setup.barrel_prob = cfg.stages[stage - 1].barrel_prob;
    if (pr.current_belt_index >= 0 && pr.current_belt_index < static_cast<int>(cfg.belts.size())) {
        setup.belt_plus_health = cfg.belts[pr.current_belt_index].plus_health;
        setup.belt_plus_energy = cfg.belts[pr.current_belt_index].plus_energy;
    }
    for (int t = 0; t < 4; ++t) {
        setup.hand[t] = pr.belted[t];
        setup.owned[t] = pr.all_cards[t];
    }
    for (int i = 0; i < 9; ++i) setup.missions.complete[i] = pr.completed_missions[i];
    setup.missions.kelp_remaining = 6 - static_cast<int>(pr.killed_kelps.size());
    for (const auto& k : pr.killed_kelps) setup.missions.killed_kelps.push_back({k.level, k.x, k.y});
    for (const auto& k : pr.killed_doodles) setup.missions.killed_doodles.push_back({k.level, k.x, k.y});
    setup.missions.which_doodle = pr.doodle_belt_index > 0 ? pr.doodle_belt_index : 1 + static_cast<int>(seed % 20);
    sim_ = std::make_unique<sbso::game::Sim>(app.assets().db(), level_, setup, seed);
    if (resume) {
        std::optional<sbso::game::Sim> restored;
        if (sbso::game::Sim::restore(app.assets().db(), level_, *resume, &restored)) sim_ = std::make_unique<sbso::game::Sim>(std::move(*restored));
        else std::fprintf(stderr, "battle: the saved battle does not match this level, starting over\n");
    }
    if (!view_.load(app.assets().extracted(), cfg.stages[stage - 1].name, level_.tileset, &err)) {
        std::fprintf(stderr, "battle: %s\n", err.c_str());
        sim_.reset();
        return;
    }
    view_.set_flag_library(app.assets().library("tourbus"));
    xtras_ = app.assets().library("tiles/xtras");
    gui_ = app.assets().library("GUI");
    bubbles_ = app.assets().library("bubbles");
    if (xtras_) cursor_ = engine::MovieClip::create(xtras_, xtras_->symbol_id("tiles.MAP_CURSOR"), &app);
    hud_ = std::make_unique<Hud>(app);
    int capacity = pr.current_belt_index >= 0 && pr.current_belt_index < static_cast<int>(cfg.belts.size()) ? cfg.belts[pr.current_belt_index].capacity : 3;
    ActionBar::Callbacks cb;
    cb.card_selected = [this](const std::string& c, bool clicked) { select_card(c, clicked); };
    cb.card_released = [this]() { release_card(); };
    cb.end_turn = [this]() { press_end_turn(); };
    bar_ = std::make_unique<ActionBar>(app, std::min(capacity, 5), cb);
    viewport_w_ = app.layout().logical_w;
    update_zoom();
    for (int i = 0; i < sim_->unit_count(); ++i) {
        const auto& u = sim_->unit(i);
        engine::LevelView::UnitVisual v;
        v.id = i;
        v.visible = u.alive;  // a resumed battle keeps its dead enemies dead
        v.type = u.type;
        v.x = static_cast<float>(u.x * kTile + kTile / 2);
        v.y = static_cast<float>(u.y * kTile + kTile / 2);
        vis_.push_back(v);
    }
    if (const engine::Library* ml = app.assets().library("menu_assets")) {
        menu_buttons_ = ClipPanel(engine::MovieClip::create(ml, ml->symbol_id("inGameMenuButtons"), &app), Matrix{}, {"cMenuButt", "cHelp", "cEyeOpen", "cEyeClosed"});
        menu_buttons_.clip()->set_child_visible("cEyeOpen", false);
        place_menu_buttons();
    }
    center_on_tile(sim_->unit(sim_->sb_id()).x, sim_->unit(sim_->sb_id()).y);
    if (!resume && !(stage == 1 && level == 1) && !app.options().skip_dialogues) {
        // Map.introAnimation
        intro_to_ = {map_x_, map_y_};
        float fx = static_cast<float>(level_.flag_x * kTile + kTile / 2) * zoom_ + map_x_, fy = static_cast<float>(level_.flag_y * kTile + kTile / 2) * zoom_ + map_y_;
        float left = -(viewport_w_ - sbso::kDesignW) / 2.0f, right = sbso::kDesignW + (viewport_w_ - sbso::kDesignW) / 2.0f;
        if (fx > left && fx < right && fy > 0 && fy < sbso::kDesignH) {
            intro_from_ = intro_to_;
            intro_inc_ = 1;
        } else {
            center_on_tile(level_.flag_x, level_.flag_y);
            intro_from_ = {map_x_, map_y_};
            float mag = std::hypot(intro_to_.x - intro_from_.x, intro_to_.y - intro_from_.y);
            intro_inc_ = mag > 10 ? 10.0f / mag : 1.0f;
        }
        intro_state_ = 1;
        intro_timer_ = 0;
        busy_ = true;
    }
    // Skip the spawn events; keep the first turn banner.
    for (const Event& e : sim_->take_events())
        if (e.kind == Event::Kind::TurnStarted) queue_.push_back(e);
    take_turn_snapshot();
    if (resume) {
        if (app.options().headless) std::fprintf(stderr, "battle: resumed from a saved turn\n");
        started_ = cutscene_seen_ = true;  // no intro dialogue / cut scene the second time around
        refresh_hud();
        bar_->refresh(*sim_);
    }
    app.sound().play_sound("start_level", false, -1);
}

int BattleScreen::logical_width(App& app) const { return app.layout().logical_w; }

// ---------------------------------------------------------------------------------------------
// camera (Map.scrollTo / centerMapOnPoint / SpongeBob.handleScrolling)
// ---------------------------------------------------------------------------------------------

void BattleScreen::update_zoom() {
    // A level narrower than the (extended) viewport would leave black bands and the HUD floating outside the map: magnify it instead.
    float raw_w = static_cast<float>(level_.width * kTile), raw_h = static_cast<float>(level_.height * kTile);
    float z = std::max({1.0f, viewport_w_ / raw_w, sbso::kDesignH / raw_h});
    z = std::ceil(z * 64.0f) / 64.0f;
    if (zoom_ > 0 && std::fabs(z - zoom_) < 1e-4f) return;
    float cx = 0, cy = 0;  // map point at the centre of the screen, kept across a change (window resize)
    if (zoom_ > 0) { cx = (sbso::kDesignW / 2.0f - map_x_) / zoom_; cy = (sbso::kDesignH / 2.0f - map_y_) / zoom_; }
    zoom_ = z;
    map_w_ = raw_w * z;
    map_h_ = raw_h * z;
    if (z > 0 && (cx != 0 || cy != 0)) scroll_to(sbso::kDesignW / 2.0f - cx * z, sbso::kDesignH / 2.0f - cy * z);
    else scroll_to(map_x_, map_y_);
}

void BattleScreen::scroll_to(float x, float y) {
    float left = -(viewport_w_ - sbso::kDesignW) / 2.0f, right = sbso::kDesignW + (viewport_w_ - sbso::kDesignW) / 2.0f;
    map_x_ = map_w_ <= right - left ? left + ((right - left) - map_w_) / 2 : std::min(left, std::max(right - map_w_, x));
    map_y_ = std::min(0.0f, std::max(sbso::kDesignH - map_h_, y));
}

void BattleScreen::center_on_tile(int tx, int ty) {
    float gx = static_cast<float>(tx * kTile + kTile / 2) * zoom_ + map_x_, gy = static_cast<float>(ty * kTile + kTile / 2) * zoom_ + map_y_;
    float cx = sbso::kDesignW / 2.0f, cy = sbso::kDesignH / 2.0f;
    float dx = cx - gx, dy = 0;
    if (gy > sbso::kDesignH - cy) dy = sbso::kDesignH - cy - gy;
    else if (gy < cy) dy = cy - gy;
    scroll_to(map_x_ + dx, map_y_ + dy);
}

void BattleScreen::follow_spongebob() {
    const auto& v = vis_[sim_->sb_id()];
    float gx = v.x * zoom_ + map_x_, gy = v.y * zoom_ + map_y_;
    float hb = 6.0f * kTile, tb = 6.0f * kTile, bb = 8.0f * kTile;
    float left = -(viewport_w_ - sbso::kDesignW) / 2.0f, right = sbso::kDesignW + (viewport_w_ - sbso::kDesignW) / 2.0f;
    float dx = 0, dy = 0;
    if (gx > right - hb) dx = right - hb - gx;
    else if (gx < left + hb) dx = left + hb - gx;
    if (gy < tb) dy = tb - gy;
    if (gy > sbso::kDesignH - bb) dy = sbso::kDesignH - bb - gy;
    if (dx != 0 || dy != 0) scroll_to(map_x_ + dx, map_y_ + dy);
}

void BattleScreen::open_dialogue(const sbso::game::Dialogue& d, DialogueScreen::Kind kind, const std::string& card, int belt, std::function<void()> then) {
    if (d.empty() || app_.options().skip_dialogues) { if (then) then(); return; }
    busy_ = true;
    rate_ = 38.0f;
    app_.open_overlay(std::make_unique<DialogueScreen>(app_, d, kind, card, belt, [this, then]() { busy_ = false; rate_ = 31.0f; if (then) then(); }));
}

// ---------------------------------------------------------------------------------------------
// events
// ---------------------------------------------------------------------------------------------

void BattleScreen::pull_events() {
    for (Event& e : sim_->take_events()) queue_.push_back(std::move(e));
}

void BattleScreen::refresh_hud() {
    const auto& sb = sim_->unit(sim_->sb_id());
    hud_->show_player(sb.health, sb.energy, sb.max_health ? sb.health * 100 / sb.max_health : 0, sb.max_energy ? sb.energy * 100 / sb.max_energy : 0, app_.progress().coins);
}

void BattleScreen::begin_move(int unit, const std::vector<Pt>& path) {
    move_.unit = unit;
    move_.path = path;
    move_.next = 0;
    busy_ = true;
    app_.sound().play_sound(unit == sim_->sb_id() ? "battle_spongebob_movement_loop" : "battle_enemy_movement_loop", false, -1);
}

void BattleScreen::tick_move() {
    if (move_.unit < 0) return;
    auto& v = vis_[move_.unit];
    if (move_.next >= move_.path.size()) {
        v.label = "ready";
        move_.unit = -1;
        busy_ = false;
        return;
    }
    const Pt& t = move_.path[move_.next];
    float tx = static_cast<float>(t.x * kTile + kTile / 2), ty = static_cast<float>(t.y * kTile + kTile / 2);
    v.label = tx < v.x ? "move_left" : "move_right";  // Unit.MOVE_ENTER: left when the next step goes left
    float dx = std::max(-5.0f, std::min(5.0f, tx - v.x)), dy = std::max(-5.0f, std::min(5.0f, ty - v.y));
    v.x += dx;
    v.y += dy;
    if (v.x == tx && v.y == ty) ++move_.next;
    if (move_.unit == sim_->sb_id()) follow_spongebob();
}

bool BattleScreen::begin_event(const Event& e) {
    using K = Event::Kind;
    switch (e.kind) {
        case K::TurnStarted: {
            banner_player_ = e.a == 0;
            hover_enemy_ = -1;
            if (gui_) banner_ = engine::MovieClip::create(gui_, gui_->symbol_id(banner_player_ ? "PLAYER_TURN_BANNER" : "ENEMY_TURN_BANNER"), &app_);
            if (!banner_player_) { bar_->show(false); hud_->hide_player(); }
            else hud_->hide_enemy();  // TurnManager: the enemy panel is hidden when the player's turn starts
            busy_ = true;
            return true;
        }
        case K::UnitSpawned: {
            if (e.a >= static_cast<int>(vis_.size())) vis_.resize(e.a + 1);
            const auto& u = sim_->unit(e.a);
            auto& v = vis_[e.a];
            v.id = e.a;
            v.type = u.type;
            v.x = static_cast<float>(u.x * kTile + kTile / 2);
            v.y = static_cast<float>(u.y * kTile + kTile / 2);
            v.visible = true;
            return false;
        }
        case K::UnitActive: {
            const auto& u = sim_->unit(e.a);
            hud_->show_enemy(u.type, u.health, u.energy, u.max_health ? u.health * 100 / u.max_health : 0, u.max_energy ? u.energy * 100 / u.max_energy : 0);
            return false;
        }
        case K::UnitMoved:
            begin_move(e.a, e.path);
            return true;
        case K::Battle: {
            const auto& db = app_.assets().db();
            const int sb = sim_->sb_id();
            const auto& att = sim_->unit(e.a);
            const auto& def = sim_->unit(e.b);
            BattleSpec sp;
            sp.attacker_type = att.type;
            sp.defender_type = def.type;
            sp.sb_attacker = e.a == sb;
            if (const auto* ac = db.card(e.text)) { sp.attack_symbol = ac->symbol; sp.attack_card_type = sbso::game::card_type_name(ac->type); }
            if (const auto* dc = db.card(e.text2)) { sp.defense_symbol = dc->symbol; if (dc->counter_damage > 0) counter_seen_ = true; }
            sp.damage = e.c;
            sp.antidamage = e.d;
            sp.defeated = (e.f & 1) != 0;
            sp.barrel_jelly = (e.f & 16) != 0;
            sp.drop = (e.f & 2) ? 1 : ((e.f & 4) ? 2 : ((e.f & 8) ? 3 : 0));
            if (sp.drop == 3) {
                switch (stage_) {
                    case 2: sp.belt_clip = "gainbelt_kelp"; break;
                    case 8: sp.belt_clip = "gainbelt_mermaidMan"; break;
                    case 9: sp.belt_clip = "gainbelt_doodlebob"; break;
                    case 3: sp.belt_clip = "gainbelt_Pirate"; break;
                    case 6: sp.belt_clip = "gainbelt_IronUnderwear"; break;
                    default: sp.drop = 0;
                }
            }
            sp.stage = stage_;
            sp.floor_intro = !counter_seen_;
            const int att_id = e.a, def_id = e.b;
            sp.attack_energy = [this, att_id, sb]() {
                const auto& a = sim_->unit(att_id);
                hud_->set_energy(a.energy, a.max_energy ? a.energy * 100 / a.max_energy : 0, att_id == sb);
            };
            sp.defend_health = [this, def_id, sb]() {
                const auto& d = sim_->unit(def_id);
                int pct = d.max_health ? d.health * 100 / d.max_health : 0;
                if (def_id == sb) hud_->set_player_health(d.health, pct);
                else hud_->set_enemy_health(d.health, pct);
            };
            const auto& foe = e.a == sb ? def : att;
            // the panels show the values from before this exchange (the sim already applied it); the callbacks animate to the new ones
            auto pct = [](int value, int max) { return max > 0 ? std::min(100, std::max(0, value * 100 / max)) : 0; };
            auto before_hp = [&](int id) { return id == e.b ? e.h0 : sim_->unit(id).health; };
            auto before_en = [&](int id) { return id == e.a ? e.e0 : sim_->unit(id).energy; };
            const int foe_id = e.a == sb ? e.b : e.a;
            hud_->show_enemy(foe.type, before_hp(foe_id), before_en(foe_id), pct(before_hp(foe_id), foe.max_health), pct(before_en(foe_id), foe.max_energy));
            const auto& me = sim_->unit(sb);
            hud_->show_player(before_hp(sb), before_en(sb), pct(before_hp(sb), me.max_health), pct(before_en(sb), me.max_energy), app_.progress().coins);
            hide_enemy_after_ = e.a == sb;
            hover_enemy_ = -1;
            animator_ = std::make_unique<BattleAnimator>(app_, std::move(sp));
            rate_ = 31.0f;  // BattleManager.battle
            busy_ = true;
            return true;
        }
        case K::UnitRemoved: {
            if (e.a < static_cast<int>(vis_.size())) {
                vis_[e.a].visible = false;
                if (bubbles_) {
                    auto fx = engine::MovieClip::create(bubbles_, bubbles_->symbol_id("unit_defeat_Bubbles"), &app_);
                    effects_.emplace_back(std::move(fx), Point{vis_[e.a].x, vis_[e.a].y});
                }
            }
            wait_ = 12;
            busy_ = true;
            return true;
        }
        case K::CoinsGained:
            save::add_coins(app_.progress(), e.c);
            return false;
        case K::CardDropped: {
            const auto* info = app_.assets().db().card(e.text);
            if (info) save::add_new_card(app_.progress(), static_cast<int>(info->type), e.text);
            auto it = app_.dialogues().card.find("card_" + std::to_string(1 + static_cast<int>(cosmetic_rng_() % 10)));
            if (it == app_.dialogues().card.end() || it->second.empty()) return false;
            open_dialogue(it->second, DialogueScreen::Kind::GainCard, e.text, 0);
            return true;
        }
        case K::BossDefeated: {
            auto it = app_.dialogues().boss_end.find(stage_);
            if (it == app_.dialogues().boss_end.end() || it->second.empty()) return false;
            open_dialogue(it->second, DialogueScreen::Kind::Plain, "", 0);
            return true;
        }
        case K::MissionCompleted: {
            int belt = e.a + 1;  // GameLevel.getGainBeltDialogue(mission + 1)
            if (belt >= 0 && belt < 10) app_.progress().gained_belts[belt] = 1;
            if (e.a >= 0 && e.a < 9) app_.progress().completed_missions[e.a] = true;
            if (belt < static_cast<int>(app_.dialogues().belt.size()) && !app_.dialogues().belt[belt].empty()) {
                open_dialogue(app_.dialogues().belt[belt], DialogueScreen::Kind::GainBelt, "", belt);
                return true;
            }
            return false;
        }
        case K::NickCardConsumed: {
            auto& nick = app_.progress().belted[3];
            auto it = std::find(nick.begin(), nick.end(), e.text);
            if (it != nick.end()) nick.erase(it);
            return false;
        }
        case K::CounterGauge: {
            busy_ = true;
            app_.open_overlay(std::make_unique<GaugeScreen>(app_, e.c, [this](double power, bool timeout) {
                if (timeout) sim_->gauge_timeout();
                else sim_->provide_gauge(power);
                pull_events();
                busy_ = false;
            }));
            return true;
        }
        case K::LevelWon:
        case K::LevelLost: {
            end_victory_ = e.kind == K::LevelWon;
            if (gui_) end_anim_ = engine::MovieClip::create(gui_, gui_->symbol_id(end_victory_ ? "VICTORY_ANI" : "DEFEAT_ANI"), &app_);
            if (end_victory_) app_.switch_music("");  // showEndBattle_Ani: switchMusic(null)
            app_.sound().play_sound(end_victory_ ? "new_area" : "i_mapafterdie", false, -1);
            bar_->show(false);
            if (end_victory_) vis_[sim_->sb_id()].label = "win";
            busy_ = true;
            return true;
        }
        default:
            return false;
    }
}

void BattleScreen::process_events() {
    while (!busy_ && !queue_.empty()) {
        Event e = std::move(queue_.front());
        queue_.pop_front();
        if (begin_event(e)) break;
    }
}

// ---------------------------------------------------------------------------------------------
// selection and highlights
// ---------------------------------------------------------------------------------------------

void BattleScreen::update_highlights() {
    highlights_.clear();
    highlight_clips_.clear();
    const std::string& card = !selected_.empty() ? selected_ : preview_;
    if (card.empty() || !sim_->can_act()) return;
    const auto* info = app_.assets().db().card(card);
    if (!info || info->effect == "HEAL") return;
    highlight_type_ = info->type;
    highlights_ = sim_->range_for(card);
    if (xtras_) {
        const char* kind = highlight_type_ == CardType::Move ? "MOVE" : (highlight_type_ == CardType::Defend ? "DEFEND" : "ATTACK");
        for (const Pt& p : highlights_)
            highlight_clips_[p.x + p.y * level_.width] = engine::MovieClip::create(xtras_, xtras_->symbol_id(std::string("tiles.") + kind), &app_);
    }
}

void BattleScreen::select_card(const std::string& card, bool clicked) {
    const auto* info = app_.assets().db().card(card);
    if (!info || !sim_->can_act() || busy_) return;
    if (!clicked) {  // rollover preview
        preview_ = card;
        update_highlights();
        return;
    }
    if (info->type == CardType::Defend) {
        if (sim_->play_defend(card)) { selected_.clear(); preview_.clear(); pull_events(); update_highlights(); }
        return;
    }
    if (info->type == CardType::Nick && info->effect == "HEAL") {
        if (sim_->play_heal(card)) { selected_.clear(); preview_.clear(); pull_events(); update_highlights(); refresh_hud(); }
        return;
    }
    selected_ = selected_ == card ? std::string() : card;  // clicking the selected card deselects it
    bar_->set_selected(selected_);
    update_highlights();
}

void BattleScreen::release_card() {
    preview_.clear();
    if (selected_.empty()) update_highlights();
}

Pt BattleScreen::tile_at(Point p) const {
    Point m = to_map(p);
    return {static_cast<int>(std::floor(m.x / kTile)), static_cast<int>(std::floor(m.y / kTile))};
}

bool BattleScreen::play_card_at(const std::string& card, Pt tile) {
    const auto* info = app_.assets().db().card(card);
    if (!info || !sim_->can_act() || busy_) return false;
    bool ok = false;
    if (info->type == CardType::Move) ok = sim_->play_move(card, tile);
    else if (info->type == CardType::Attack || info->type == CardType::Nick) ok = sim_->play_attack(card, tile);
    if (ok) {
        selected_.clear();
        preview_.clear();
        bar_->set_selected("");
        pull_events();
        update_highlights();
    }
    return ok;
}

void BattleScreen::debug_end_level(bool victory) {
    queue_.push_back(Event(victory ? Event::Kind::LevelWon : Event::Kind::LevelLost));
}

void BattleScreen::press_end_turn() {
    if (!sim_->can_act() || busy_) return;
    selected_.clear();
    preview_.clear();
    bar_->set_selected("");
    highlights_.clear();
    sim_->end_turn();
    pull_events();
    take_turn_snapshot();  // a turn boundary
}

// ---------------------------------------------------------------------------------------------
// input
// ---------------------------------------------------------------------------------------------

void BattleScreen::pointer_move(App& app, Point p) {
    pointer_ = p;
    if (!sim_) return;
    menu_buttons_.pointer_move(app, p);
    bar_->pointer_move(to_bar(p));
    update_hover_enemy();
}

// Port addition: the original only shows the enemy panel during its turn and in fights; on the player's turn it is shown while the
// pointer rests on an enemy so its health can be checked before attacking.
void BattleScreen::update_hover_enemy() {
    int foe = -1;
    if (sim_ && !finished_ && !busy_ && !ui_hidden_ && sim_->can_act() && !animator_) {
        Pt t = tile_at(pointer_);
        for (int i = 0; i < sim_->unit_count(); ++i) {
            const auto& u = sim_->unit(i);
            // only real foes: scenery units (the 1-1 chest and Patrick) have no portrait and would show the previous unit's face
            if (u.alive && u.is_ai && u.x == t.x && u.y == t.y && hud_->has_face(u.type)) { foe = i; break; }
        }
    }
    if (foe == hover_enemy_) return;
    hover_enemy_ = foe;
    if (foe >= 0) {
        const auto& u = sim_->unit(foe);
        hud_->show_enemy(u.type, u.health, u.energy, u.max_health ? u.health * 100 / u.max_health : 0, u.max_energy ? u.energy * 100 / u.max_energy : 0);
    } else {
        hud_->hide_enemy();
    }
}

void BattleScreen::pointer_down(App& app, Point p) {
    pointer_ = p;
    if (!sim_ || finished_) return;
    menu_buttons_.pointer_down(app, p);
    bar_->pointer_down(to_bar(p));
    swipe_from_ = p;
    swipe_ticks_ = 0;
    swiping_ = true;
}

void BattleScreen::pointer_up(App& app, Point p) {
    pointer_ = p;
    if (!sim_) return;
    if (finished_) { leave(end_victory_ ? Exit::Victory : Exit::Defeat); return; }
    std::string b = menu_buttons_.pointer_up(app, p);
    if (!b.empty()) {
        if (b == "cMenuButt") { app.sound().play_sound("mapscreen_click_menu_button", false, -1); open_menu(); }
        else {
            app.sound().play_sound("button_click", false, -1);
            if (b == "cHelp") show_help(false);
            else show_ui(b == "cEyeOpen");
        }
        return;
    }
    // port addition (touch): a quick horizontal swipe anywhere turns the card carousel, like its arrows
    if (swiping_) {
        swiping_ = false;
        float dx = p.x - swipe_from_.x, dy = p.y - swipe_from_.y;
        if (std::fabs(dx) > 60 && std::fabs(dy) < std::fabs(dx) * 0.6f && swipe_ticks_ < 40 && bar_->visible()) {
            bar_->pointer_move({-1000, -1000});
            bar_->cycle_type(dx < 0);
            return;
        }
    }
    if (bar_->pointer_up(to_bar(p))) return;
    if (!selected_.empty() && sim_->can_act() && !busy_) {
        Pt t = tile_at(p);
        if (std::find(highlights_.begin(), highlights_.end(), t) != highlights_.end()) play_card_at(selected_, t);
    }
}

// Phones: the card bar is drawn bigger around its bottom-centre anchor; pointers are mapped back into its original space.
namespace { constexpr float kBarScale = kMobile ? 1.3f : 1.0f, kMenuScale = kMobile ? 1.8f : 1.0f; }
Matrix BattleScreen::bar_matrix() const {
    const float ax = sbso::kDesignW / 2.0f, ay = sbso::kDesignH;
    return Matrix{kBarScale, 0, 0, kBarScale, ax - ax * kBarScale, ay - ay * kBarScale};
}
Point BattleScreen::to_bar(Point p) const { return bar_matrix().inverse().apply(p); }

// Stage x (0..640 is the original screen) of the safe-area edges of the extended viewport.
float BattleScreen::safe_left() const { return app_.layout().ui_left - (viewport_w_ - sbso::kDesignW) / 2.0f; }
float BattleScreen::safe_right() const { return app_.layout().ui_right - (viewport_w_ - sbso::kDesignW) / 2.0f; }

void BattleScreen::place_menu_buttons() {
    if (!menu_buttons_.valid()) return;
    // GameScreen.initGameMenuButtons: top right corner, 3px from the edge (re-anchored to the extended viewport).
    // Inside the safe area (iPhone: clear of the rounded corner and the Dynamic Island).
    engine::Rect b = menu_buttons_.clip()->bounds();
    float right = safe_right(), k = kMenuScale;  // phones: bigger, finger-sized
    menu_buttons_.set_place(Matrix{k, 0, 0, k, right - 3 - b.x1 * k, 3 - b.y0 * k});
}

void BattleScreen::show_ui(bool on) {  // GameLevel.showHUDElements
    ui_hidden_ = !on;
    if (menu_buttons_.valid()) {
        menu_buttons_.clip()->set_child_visible("cEyeClosed", on);
        menu_buttons_.clip()->set_child_visible("cEyeOpen", !on);
    }
    if (!on) bar_->show(false);
}

void BattleScreen::show_help(bool forced) {  // GameLevel.showHelp: the tutorial for the equipped belt's capacity
    if (app_.options().skip_dialogues && forced) return;
    if (ui_hidden_) show_ui(true);
    const auto& belts = app_.config().belts;
    int bi = app_.progress().current_belt_index;
    int cap = bi >= 0 && bi < static_cast<int>(belts.size()) ? belts[bi].capacity : 3;
    if (forced) app_.session().help_shown = true;
    busy_ = true;
    app_.open_overlay(std::make_unique<HelpScreen>(app_, "help_" + std::to_string(cap), forced, [this](bool instructions) {
        busy_ = false;
        if (instructions) app_.open_overlay(std::make_unique<InstructionsScreen>(app_, nullptr));
    }));
}

void BattleScreen::open_menu() {
    app_.open_overlay(std::make_unique<InGameMenuScreen>(app_, [this](const std::string& w) { menu_choice(w); }));
}

void BattleScreen::menu_choice(const std::string& what) {
    if (what == "exit") leave(Exit::Quit);
    else if (what == "options") app_.open_overlay(std::make_unique<OptionsScreen>(app_));
    else if (what == "saves") {  // a manual slot stores the last turn boundary of this level, like quitting does
        if (sim_ && !finished_ && !turn_snapshot_.empty()) app_.store_battle_snapshot(stage_, level_num_, turn_snapshot_);
        flow::saved_games(app_);
    }
    else if (what == "instructions") app_.open_overlay(std::make_unique<InstructionsScreen>(app_, nullptr));
}

void BattleScreen::take_turn_snapshot() {
    if (sim_ && sim_->can_snapshot()) turn_snapshot_ = sim_->snapshot();
}

void BattleScreen::on_suspend(App& app) {
    if (sim_ && !finished_ && !turn_snapshot_.empty()) app.store_battle_snapshot(stage_, level_num_, turn_snapshot_);
}

void BattleScreen::leave(Exit how) {
    // Level over (won / lost): the mid-level save is obsolete. Quitting keeps the last turn boundary.
    const auto& m = sim_->missions();
    save::Progress& pr = app_.progress();
    pr.killed_kelps.clear();
    for (const auto& k : m.killed_kelps) pr.killed_kelps.push_back({k.level, k.x, k.y});
    pr.killed_doodles.clear();
    for (const auto& k : m.killed_doodles) pr.killed_doodles.push_back({k.level, k.x, k.y});
    if (how == Exit::Quit && !turn_snapshot_.empty()) app_.store_battle_snapshot(stage_, level_num_, turn_snapshot_);
    else app_.clear_battle_snapshot();
    if (exit_) { auto h = exit_; h(how); return; }
    app_.set_screen(std::make_unique<TitleScreen>(app_, true));
}

void BattleScreen::key_down(App&, int key) {
    if (key == 27 && sim_ && !finished_) open_menu();  // GameScreen._handleKey: ESC
}

// ---------------------------------------------------------------------------------------------
// frame
// ---------------------------------------------------------------------------------------------

void BattleScreen::tick(App&) {
    if (!sim_) return;
    if (intro_state_ == 1) {
        if (++intro_timer_ > 31) {
            if (intro_inc_ < 1) { intro_state_ = 2; intro_prog_ = 0; }
            else { intro_state_ = 0; busy_ = false; }
        }
    } else if (intro_state_ == 2) {
        intro_prog_ += intro_inc_;
        auto cos_interp = [](float a, float b, float t) { float k = (1 - std::cos(t * 3.14159265f)) / 2; return a * (1 - k) + b * k; };
        if (intro_prog_ < 1) {
            map_x_ = cos_interp(intro_from_.x, intro_to_.x, intro_prog_);
            map_y_ = cos_interp(intro_from_.y, intro_to_.y, intro_prog_);
        } else {
            map_x_ = intro_to_.x;
            map_y_ = intro_to_.y;
            intro_state_ = 0;
            busy_ = false;
        }
    }
    if (intro_state_ != 0) { view_.tick_units(); return; }
    if (!started_ && stage_ == 1 && level_num_ == 1 && !cutscene_seen_ && !app_.options().skip_dialogues) {
        cutscene_seen_ = true;  // GameLevel.loadInterface: level 1-1 opens with the Krusty Krab cut scene
        busy_ = true;
        app_.open_overlay(std::make_unique<KrustyCutScene>(app_, [this]() { busy_ = false; rate_ = 31.0f; }));
        return;
    }
    if (busy_ && !started_) return;
    if (!started_) {  // GameLevel.introAnimationDone: stage intro dialogue before the first turn
        started_ = true;
        if (const auto* d = app_.dialogues().intro_for(stage_, level_num_)) {
            // GameLevel.DIALOGUE_OVER "stage_intro": the scenery (1-1: the atlantean, Patrick and the chest) bubbles off
            auto bubble_off = [this]() { sim_->bubble_off_list(); pull_events(); };
            if (app_.options().skip_dialogues) bubble_off();
            else { open_dialogue(*d, DialogueScreen::Kind::Plain, "", 0, bubble_off); return; }
        }
    }
    view_.tick_units();
    hud_->tick();
    update_hover_enemy();
    menu_buttons_.tick();
    bar_->tick();
    if (swiping_) ++swipe_ticks_;
    if (cursor_) cursor_->tick();
    for (auto& [idx, c] : highlight_clips_) {
        c->tick();
        if (c->current_frame() == c->total_frames() - 1) c->stop();
    }
    for (auto& fx : effects_) fx.first->tick();
    effects_.erase(std::remove_if(effects_.begin(), effects_.end(), [](auto& f) { return f.first->current_frame() == f.first->total_frames() - 1; }), effects_.end());

    if (banner_) {
        banner_->tick();
        int f = banner_->current_frame();
        if (f == 8) app_.sound().play_sound("x_load", false, -1);
        if (f == (banner_player_ ? 41 : 36)) app_.sound().play_sound("x_loadtrans", false, -1);
        if (f == banner_->total_frames() - 1) {
            banner_.reset();
            busy_ = false;
            if (banner_player_) {
                // autosave at the start of every player turn (a safe boundary: nothing is animating), not only when leaving or
                // going to the background
                take_turn_snapshot();
                if (!finished_ && !turn_snapshot_.empty()) app_.store_battle_snapshot(stage_, level_num_, turn_snapshot_);
                if (!app_.session().help_shown && app_.progress().unlocked_stages[0][0] != save::kCleared) show_help(true);  // fForceShowHelp
                if (!ui_hidden_) bar_->show(true);
                refresh_hud();
                bar_->refresh(*sim_);
            }
        }
    } else if (end_anim_) {
        end_anim_->tick();
        if (end_anim_->current_frame() == end_anim_->total_frames() - 1) {  // endBattleAni_CB: the last frame leaves the level by itself (no click needed)
            end_anim_->stop();
            finished_ = true;
            busy_ = false;
            leave(end_victory_ ? Exit::Victory : Exit::Defeat);
        }
    } else if (move_.unit >= 0) {
        tick_move();
    } else if (animator_) {
        animator_->tick();
        if (animator_->done()) {
            animator_.reset();
            busy_ = false;
            rate_ = 38.0f;
            if (hide_enemy_after_) hud_->hide_enemy();  // BattleManager: back on the player's turn the enemy panel goes away
            hide_enemy_after_ = false;
        }
    } else if (wait_ > 0) {
        if (--wait_ == 0) busy_ = false;
    }
    process_events();
    if (!busy_ && queue_.empty() && sim_->can_act()) {
        bar_->refresh(*sim_);
        bar_->set_enabled(true);
        if (!bar_->visible() && !ui_hidden_) bar_->show(true);
        const auto& sb = sim_->unit(sim_->sb_id());
        hud_->set_energy(sb.energy, sb.max_energy ? sb.energy * 100 / sb.max_energy : 0, true);
    } else {
        bar_->set_enabled(false);
    }
    // cursor (Map.onEnterFrame)
    cursor_tile_ = {-1, -1};
    if (sim_->can_act() && !busy_) {
        Pt t = tile_at(pointer_);
        bool valid = false;
        if (!highlights_.empty() && std::find(highlights_.begin(), highlights_.end(), t) != highlights_.end()) valid = true;
        if (valid) cursor_tile_ = t;
    }
}

void BattleScreen::draw(App& app, std::vector<engine::DrawCmd>& out) {
    if (!sim_) return;
    viewport_w_ = logical_width(app);
    update_zoom();
    place_menu_buttons();
    size_t start = out.size();
    view_.build_tiles(level_, out);
    // highlight tiles
    if (xtras_) {
        for (const Pt& p : highlights_) {
            auto it = highlight_clips_.find(p.x + p.y * level_.width);
            if (it == highlight_clips_.end()) continue;
            it->second->collect(out, Matrix{1, 0, 0, 1, static_cast<float>(p.x * kTile), static_cast<float>(p.y * kTile)}, engine::ColorTransform{});
        }
        if (cursor_ && cursor_tile_.x >= 0) cursor_->collect(out, Matrix{1, 0, 0, 1, static_cast<float>(cursor_tile_.x * kTile), static_cast<float>(cursor_tile_.y * kTile)}, engine::ColorTransform{});
    }
    view_.build_units(vis_, out);
    for (auto& [fx, pos] : effects_) fx->collect(out, Matrix{1, 0, 0, 1, pos.x, pos.y}, engine::ColorTransform{});
    engine::transform_all(out, start, Matrix{zoom_, 0, 0, zoom_, map_x_, map_y_});
    // overlay: HUD on the left edge, action bar, banner, end animation
    float left = -(viewport_w_ - sbso::kDesignW) / 2.0f;
    size_t bar_start = out.size();
    bar_->draw(out);
    if (kBarScale != 1.0f) engine::transform_all(out, bar_start, bar_matrix());
    if (animator_) animator_->draw(out, left);
    if (!ui_hidden_) hud_->draw(out, Matrix{1, 0, 0, 1, safe_left(), 0});
    if (banner_) {
        // the band (depth 1) was authored for the 640 px stage: stretch it across the whole extended viewport
        float extra = (viewport_w_ - sbso::kDesignW) / 2.0f;
        banner_->stretch_depth_x(1, -175.0f - extra - 8.0f, sbso::kDesignW - 175.0f + extra + 8.0f);
        banner_->collect(out, Matrix{1, 0, 0, 1, 175.0f, static_cast<float>(sbso::kDesignH / 2)}, engine::ColorTransform{});
    }
    if (end_anim_) {
        // VICTORY slides off to the right edge of the 640 px stage in its last frames; on a wider scene it has to travel the extra width too
        float dx = 0;
        if (end_victory_) {
            float extra = (viewport_w_ - sbso::kDesignW) / 2.0f;
            int f = end_anim_->current_frame();
            if (extra > 0 && f >= 53) dx = extra * std::min(1.0f, (f - 53) / 6.0f);
        }
        end_anim_->collect(out, Matrix{1, 0, 0, 1, dx, end_victory_ ? 70.0f : 0.0f}, engine::ColorTransform{});
    }
    if (!end_anim_) menu_buttons_.draw(out);
}

}  // namespace sbso::app
