#include "hud.h"

#include <cmath>

namespace sbso::app {
namespace {

int clamp_pct(int v) { return v < 1 ? 1 : (v > 101 ? 101 : v); }

}  // namespace

Hud::Hud(App& app) : app_(app) {
    lib_ = app.assets().library("HUD");
    if (!lib_) return;
    sb_.clip = engine::MovieClip::create(lib_, lib_->symbol_id("HUD.Status.sb"), &app);
    enemy_.clip = engine::MovieClip::create(lib_, lib_->symbol_id("HUD.Status.enemy"), &app);
    sb_.clip->stop();
    enemy_.clip->stop();
    for (Panel* p : {&sb_, &enemy_}) {
        three_digits(p->clip->find("holder.healthNum"), 100);
        three_digits(p->clip->find("holder.powNum"), 100);
    }
    set_coins(0);
}

void Hud::three_digits(engine::MovieClip* holder, int value) {
    if (!holder) return;
    // HUD.setThreeDigitDisplay: leading zeros hidden, digit d shows frame d (0 -> frame 10).
    std::string s = std::to_string(value);
    while (s.size() < 3) s = "0" + s;
    int pad = 3 - static_cast<int>(std::to_string(value).size());
    for (int i = 0; i < 3 && i < static_cast<int>(s.size()); ++i) {
        engine::MovieClip* n = holder->child("cN" + std::to_string(i));
        if (!n) continue;
        if (i < pad) { n->set_visible(false); continue; }
        n->set_visible(true);
        int d = s[i] - '0';
        n->goto_and_stop(d == 0 ? 10 : d);
    }
}

void Hud::set_coins(int coins) {
    engine::MovieClip* c = sb_.clip ? sb_.clip->find("holder.cCoins") : nullptr;
    if (!c) return;
    coins = coins > 999 ? 999 : coins;
    three_digits(c, coins);
    // the coin counter is right-aligned: shift it for short numbers (HUD.setCoinDisplay)
    static float base_x = 0;
    engine::MovieClip* holder = sb_.clip->find("holder");
    float x, y;
    if (holder && holder->get_child_xy("cCoins", &x, &y)) {
        if (base_x == 0) base_x = x;
        holder->set_child_xy("cCoins", coins < 10 ? base_x - 16 : (coins < 100 ? base_x - 9 : base_x), y);
    }
}

void Hud::bar_to(Panel& p, const char* bar, int pct, bool animate) {
    engine::MovieClip* b = p.clip->find(std::string("holder.") + bar);
    pct = clamp_pct(pct);
    bool health = std::string(bar) == "health_bar";
    (health ? p.target_health : p.target_energy) = pct;
    double& inc = health ? p.health_inc : p.energy_inc;
    if (!b) {  // panel parked on frame 1: the bar appears (full) when the intro starts, so start easing from the right value
        if (!animate) inc = pct + 1;
        return;
    }
    if (!animate || b->current_frame() + 1 == pct) {
        b->goto_and_stop(pct);
        b->set_visible(pct > 1);
        inc = pct;
    } else {
        inc = b->current_frame() + 1;  // moveBar eases towards the target in tick()
    }
}

void Hud::ease_bar(engine::MovieClip* b, int target, double& inc) {
    if (!b) return;
    int goal = target + 1 > b->total_frames() ? b->total_frames() : target + 1;  // getBarTarget(bar) + 1
    if (std::fabs(inc - goal) > 2) {
        inc -= (inc - goal) * 0.15;
        b->goto_and_stop(static_cast<int>(std::lround(inc)));
    } else {
        b->goto_and_stop(goal);
        inc = goal;
        if (goal == 1) b->set_visible(false);
    }
}

bool Hud::animations_done() const {
    auto done = [](const Panel& p, const char* bar, int target) {
        engine::MovieClip* b = p.clip ? p.clip->find(std::string("holder.") + bar) : nullptr;
        return !b || b->current_frame() + 1 == target || (target <= 1 && !b->visible());
    };
    return done(sb_, "health_bar", sb_.target_health) && done(sb_, "energy_bar", sb_.target_energy) &&
           done(enemy_, "health_bar", enemy_.target_health) && done(enemy_, "energy_bar", enemy_.target_energy);
}

void Hud::hold_intro(Panel& p) {  // addFrameScript(8, stop): the intro stops on frame 9
    if (p.clip && p.visible && p.clip->current_frame() == 8) p.clip->stop();
}

void Hud::tick() {
    for (Panel* p : {&sb_, &enemy_}) {
        if (!p->clip) continue;
        p->clip->tick();
        hold_intro(*p);
        ease_bar(p->clip->find("holder.health_bar"), p->target_health, p->health_inc);
        ease_bar(p->clip->find("holder.energy_bar"), p->target_energy, p->energy_inc);
        if (!p->visible && p->clip->current_frame() == 1) p->clip->stop();  // _removePlayerHUD/_removeEnemyHUD (frame script on frame 2)
    }
}

void Hud::show_player(int health, int energy, int health_pct, int energy_pct, int coins) {
    if (!sb_.clip) return;
    if (sb_.clip->current_frame() < 8) sb_.clip->goto_and_play(2);
    sb_.target_health = clamp_pct(health_pct);
    sb_.target_energy = clamp_pct(energy_pct);
    bar_to(sb_, "health_bar", health_pct, false);
    bar_to(sb_, "energy_bar", energy_pct, false);
    three_digits(sb_.clip->find("holder.healthNum"), health);
    three_digits(sb_.clip->find("holder.powNum"), energy);
    set_coins(coins);
    sb_.visible = true;
}

void Hud::set_player_health(int health, int pct) {
    if (!sb_.clip) return;
    bar_to(sb_, "health_bar", pct, true);
    three_digits(sb_.clip->find("holder.healthNum"), health);
}

void Hud::set_energy(int value, int pct, bool player) {
    Panel& p = player ? sb_ : enemy_;
    if (!p.clip) return;
    bar_to(p, "energy_bar", pct, true);
    three_digits(p.clip->find("holder.powNum"), value);
}

bool Hud::has_face(const std::string& type) {
    engine::MovieClip* f = enemy_.clip ? enemy_.clip->find("holder.faces") : nullptr;
    return f && f->frame_of_label(type) >= 0;
}

void Hud::show_enemy(const std::string& type, int health, int energy, int health_pct, int energy_pct) {
    if (!enemy_.clip) return;
    if (enemy_.clip->current_frame() == 1) enemy_.clip->goto_and_play(2);
    if (engine::MovieClip* f = enemy_.clip->find("holder.faces")) f->goto_label_and_stop(type);
    bar_to(enemy_, "health_bar", health_pct, false);
    bar_to(enemy_, "energy_bar", energy_pct, false);
    three_digits(enemy_.clip->find("holder.healthNum"), health);
    three_digits(enemy_.clip->find("holder.powNum"), energy);
    if (!enemy_.visible) enemy_.clip->goto_and_play(2);
    enemy_.visible = true;
}

void Hud::set_enemy_health(int health, int pct) {
    if (!enemy_.clip) return;
    bar_to(enemy_, "health_bar", pct, true);
    three_digits(enemy_.clip->find("holder.healthNum"), health);
}

void Hud::hide_player() {
    if (!sb_.clip || !sb_.visible) return;
    sb_.clip->goto_and_play(10);  // plays the "out" animation, then is hidden
    sb_.visible = false;
}

void Hud::hide_enemy() {
    if (!enemy_.clip || !enemy_.visible) return;
    enemy_.clip->goto_and_play(10);
    enemy_.visible = false;
}

void Hud::draw(std::vector<engine::DrawCmd>& out, const engine::Matrix& m) {
    // Panels are drawn while visible or while their "out" animation plays.
    if (sb_.clip && (sb_.visible || sb_.clip->playing())) {
        engine::Matrix mm = m * engine::Matrix{1, 0, 0, 1, 0, -35};
        sb_.clip->collect(out, mm, engine::ColorTransform{});
    }
    if (enemy_.clip && (enemy_.visible || enemy_.clip->playing())) {
        engine::Matrix mm = m * engine::Matrix{1, 0, 0, 1, 40, -30};
        enemy_.clip->collect(out, mm, engine::ColorTransform{});
    }
}

}  // namespace sbso::app
