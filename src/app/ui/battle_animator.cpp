#include "battle_animator.h"

#include <cmath>

namespace sbso::app {
using engine::Matrix;

namespace {

const char* floor_name(int stage) {
    switch (stage) {
        case 1: return "floor_LtGreen";
        case 2: return "floor_ArmyGreen";
        case 4: return "floor_DrkGreen";
        case 5: case 6: return "floor_BabyBlue";
        case 7: return "floor_Palace";
        default: return "floor_Teal";  // 3, 8, 9
    }
}

}  // namespace

std::unique_ptr<engine::MovieClip> BattleAnimator::make(const std::string& cls) {
    int id = -1;
    const engine::Library* lib = app_.assets().find_class(cls, &id);
    if (!lib || id < 0) return nullptr;
    return engine::MovieClip::create(lib, id, &app_);
}

std::unique_ptr<engine::MovieClip> BattleAnimator::make_with_floor(const std::string& cls) {
    auto c = make(cls);
    if (!c) return nullptr;
    // `floor.removeChildAt(0); floor.addChild(new fFloorGraphic())`
    if (auto floor = make(std::string("battle.") + floor_name(spec_.stage))) c->replace_child_content("floor", std::move(floor));
    return c;
}

BattleAnimator::BattleAnimator(App& app, BattleSpec spec) : app_(app), spec_(std::move(spec)) {
    back_.rgba[0] = back_.rgba[1] = back_.rgba[2] = 0;
    back_.rgba[3] = 0.75f;
    back_.contours.push_back({{0, 0}, {static_cast<float>(sbso::kDesignW), 0}, {static_cast<float>(sbso::kDesignW), static_cast<float>(sbso::kDesignH)}, {0, static_cast<float>(sbso::kDesignH)}});
    if (spec_.floor_intro) {
        floor_a_ = make_with_floor("battle.floor_anim");
        floor_b_ = make_with_floor("battle.floor_anim");
        if (floor_a_) floor_a_->goto_and_play(1);
        if (floor_b_) floor_b_->goto_and_play(1);
    }
    if (!floor_a_ || !floor_b_) { floor_a_.reset(); floor_b_.reset(); start_fight(); }
}

void BattleAnimator::add_balloon(float x, float y, int amount, const char* label) {
    Balloon b;
    b.clip = make("battle.TEXT_BALLOONS");
    if (!b.clip) return;
    b.clip->goto_label_and_stop(label);
    unsigned n1 = static_cast<unsigned>(amount * 0.1);                       // BattleTextBalloon: uint(param3 * 0.1)
    unsigned n2 = static_cast<unsigned>((amount * 0.1 - n1) * 10);           // uint((param3 * 0.1 - num1) * 10)
    b.num1 = static_cast<int>(n1);
    b.num2 = static_cast<int>(n2);
    b.x = x;
    b.y = y;
    balloons_.push_back(std::move(b));
}

void BattleAnimator::start_fight() {
    phase_ = Phase::Fight;
    const float W = sbso::kDesignW, H = sbso::kDesignH;
    const float enemy_x = W / 2, sb_x = W / 2 - 192, anim_y = H / 2;
    const bool def_is_sb = !spec_.sb_attacker;

    std::string hit = spec_.defeated ? (spec_.barrel_jelly ? "_jelly_surprise" : "_defeat") : "_hit";
    if (spec_.antidamage == 0) {
        defend_ = make_with_floor("battle." + spec_.defender_type + hit);
        if (!defend_) defend_ = make_with_floor("battle.jelly_red" + hit);
    } else if (!spec_.defeated) {
        defend_ = make_with_floor("battle." + spec_.defense_symbol);
    } else {
        defend_ = make_with_floor("battle." + spec_.defender_type + hit + "_" + spec_.defense_symbol);
    }
    std::string asset = spec_.attack_symbol;
    if (spec_.attacker_type == "barrel") asset = "barrel_robot_attack";
    else if (spec_.attack_card_type == "DEFEND") asset += "_attack";
    attack_ = make_with_floor("battle." + asset);
    if (!attack_) attack_ = make_with_floor("battle.jelly_pink_attack");

    float off_x = 0, off_y = 0, aoff_x = 0, aoff_y = 0;
    if (defend_) {
        defend_->get_child_xy("floor", &off_x, &off_y);
        defend_m_ = Matrix{1, 0, 0, 1, (def_is_sb ? sb_x : enemy_x) - off_x, anim_y - off_y};
        defend_->goto_and_play(1);
    }
    floor_x_ = off_x;
    floor_y_ = off_y;
    float side = -1;
    if (attack_) {
        attack_->get_child_xy("floor", &aoff_x, &aoff_y);
        if (spec_.sb_attacker && spec_.attack_card_type == "NICK") {
            float fw = 0;
            if (auto* fl = attack_->child("floor")) fw = fl->bounds().w();
            attack_m_ = Matrix{-1, 0, 0, 1, sb_x + aoff_x + fw, anim_y - aoff_y};
            side = 1;
        } else {
            attack_m_ = Matrix{1, 0, 0, 1, (spec_.sb_attacker ? sb_x : enemy_x) - aoff_x, anim_y - aoff_y};
            float def_gx = defend_m_.tx + off_x, att_gx = attack_m_.tx + aoff_x;
            side = def_gx < att_gx ? -1 : 1;
        }
        attack_->goto_and_play(1);
    }
    if (spec_.antidamage != 0) add_balloon(side == -1 ? off_x - 55 : off_x + 180, off_y - 150, spec_.antidamage, "shield");
    add_balloon(side == -1 ? off_x - 75 : off_x + 200, off_y - 75, spec_.damage, "damage");
}

void BattleAnimator::tick_balloons() {
    for (auto& b : balloons_) {
        b.clip->tick();
        if (b.set) continue;
        engine::MovieClip* t = b.clip->child("cTextAnim");
        if (t && t->current_frame() >= t->total_frames() - 1) {  // BattleTextBalloon.onEnterFrame
            if (auto* n1 = t->child("num1")) n1->goto_and_stop(b.num1 + 1);
            if (auto* n2 = t->child("num2")) n2->goto_and_stop(b.num2 + 1);
            t->stop();
            b.set = true;
        }
    }
}

void BattleAnimator::tick() {
    if (phase_ == Phase::Floor) {
        if (floor_a_) { floor_a_->tick(); if (at_end(floor_a_.get())) floor_a_->stop(); }
        if (floor_b_) floor_b_->tick();
        if (at_end(floor_b_.get())) { floor_a_.reset(); floor_b_.reset(); start_fight(); }
        return;
    }
    if (phase_ != Phase::Fight) return;
    for (auto* c : {attack_.get(), defend_.get(), drop_.get()}) {
        if (!c) continue;
        c->tick();
        if (at_end(c)) c->stop();
    }
    tick_balloons();
    if (attack_) {
        if (!energy_fired_ && attack_->current_frame() >= 5) { energy_fired_ = true; if (spec_.attack_energy) spec_.attack_energy(); }
        if (!health_fired_ && attack_->current_frame() >= 25) { health_fired_ = true; if (spec_.defend_health) spec_.defend_health(); }
    } else {
        if (!energy_fired_) { energy_fired_ = true; if (spec_.attack_energy) spec_.attack_energy(); }
        if (!health_fired_) { health_fired_ = true; if (spec_.defend_health) spec_.defend_health(); }
    }
    // monitor_defeatCardsCoins: when the "cBigHit" clip reaches frame 5 the loot appears next to the defeated unit
    if (!drop_started_ && spec_.defeated && spec_.drop != 0 && defend_) {
        if (auto* hit = defend_->child("cBigHit")) {
            if (hit->current_frame() >= 4) {
                drop_started_ = true;
                std::string cls = spec_.drop == 1 ? "battle.defeatedEnemyCoins" : (spec_.drop == 2 ? "battle.defeatedEnemyCard" : "battle." + spec_.belt_clip);
                drop_ = make(cls);
                if (drop_) {
                    if (spec_.drop == 1) app_.sound().play_sound("battle_coins_drop", false, -1);
                    float fx = floor_x_ + (spec_.drop == 3 ? 185.0f : 55.0f), fy = floor_y_ - (spec_.drop == 3 ? 65.0f : 55.0f);
                    drop_m_ = defend_m_ * Matrix{1, 0, 0, 1, fx, fy};
                    drop_->goto_and_play(1);
                }
            }
        }
    }
    bool drop_pending = spec_.defeated && spec_.drop != 0 && defend_ && defend_->child("cBigHit") && !drop_started_;
    if (at_end(attack_.get()) && at_end(defend_.get()) && !drop_pending && (!drop_ || at_end(drop_.get()))) phase_ = Phase::Done;
}

void BattleAnimator::draw(std::vector<engine::DrawCmd>& out, float x_offset) {
    // x_offset is the (negative) x of the viewport's left edge in stage coordinates: the clips stay centred on the 640 px stage
    // while the dark backdrop covers the whole extended viewport.
    Matrix origin{};
    const float x0 = x_offset, x1 = sbso::kDesignW - x_offset, y1 = sbso::kDesignH;
    back_.contours.assign(1, {{x0, 0}, {x1, 0}, {x1, y1}, {x0, y1}});
    engine::DrawCmd d;
    d.kind = engine::DrawCmd::Kind::Solid;
    d.solid = &back_;
    d.m = origin;
    out.push_back(d);
    if (phase_ == Phase::Floor) {
        const float W = sbso::kDesignW, H = sbso::kDesignH;
        if (floor_a_) floor_a_->collect(out, origin * Matrix{1, 0, 0, 1, W / 2 - 192, H / 2}, engine::ColorTransform{});
        if (floor_b_) floor_b_->collect(out, origin * Matrix{1, 0, 0, 1, W / 2, H / 2}, engine::ColorTransform{});
        return;
    }
    if (defend_) {
        defend_->collect(out, origin * defend_m_, engine::ColorTransform{});
        for (auto& b : balloons_) b.clip->collect(out, origin * defend_m_ * Matrix{1, 0, 0, 1, b.x, b.y}, engine::ColorTransform{});
        if (drop_) drop_->collect(out, origin * drop_m_, engine::ColorTransform{});
    }
    if (attack_) attack_->collect(out, origin * attack_m_, engine::ColorTransform{});
}

}  // namespace sbso::app
