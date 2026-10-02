// Port of the animation half of BattleManager.as (setupAnimations, floor intro, text balloons, drops).
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/app.h"

namespace sbso::app {

struct BattleSpec {
    std::string attacker_type, defender_type;
    bool sb_attacker = false;
    std::string attack_symbol;      // symbol of the card being played (Card.symbol)
    std::string attack_card_type;   // "ATTACK", "NICK", "DEFEND"
    std::string defense_symbol;     // shield symbol, only used when antidamage != 0
    int damage = 0, antidamage = 0;
    bool defeated = false, barrel_jelly = false;
    int drop = 0;                   // 0 none, 1 coins, 2 card, 3 belt
    std::string belt_clip;          // "gainbelt_kelp", ... when drop == 3
    int stage = 1;
    bool floor_intro = true;
    std::function<void()> attack_energy;   // attack frame 5: _showAttackEnergy
    std::function<void()> defend_health;   // attack frame 25: _showDefendHealth
};

class BattleAnimator {
public:
    BattleAnimator(App& app, BattleSpec spec);
    void tick();
    void draw(std::vector<engine::DrawCmd>& out, float x_offset);
    bool done() const { return phase_ == Phase::Done; }

private:
    enum class Phase { Floor, Fight, Done };
    std::unique_ptr<engine::MovieClip> make(const std::string& cls);
    std::unique_ptr<engine::MovieClip> make_with_floor(const std::string& cls);
    void start_fight();
    void add_balloon(float x, float y, int amount, const char* label);
    void tick_balloons();
    static bool at_end(const engine::MovieClip* c) { return !c || c->current_frame() >= c->total_frames() - 1; }

    struct Balloon { std::unique_ptr<engine::MovieClip> clip; float x, y; int num1, num2; bool set = false; };

    App& app_;
    BattleSpec spec_;
    Phase phase_ = Phase::Floor;
    std::unique_ptr<engine::MovieClip> floor_a_, floor_b_;
    std::unique_ptr<engine::MovieClip> defend_, attack_, drop_;
    engine::Matrix defend_m_, attack_m_, drop_m_;
    std::vector<Balloon> balloons_;
    bool energy_fired_ = false, health_fired_ = false, drop_started_ = false;
    float floor_x_ = 0, floor_y_ = 0;  // defend clip's floor offset
    engine::SolidPath back_;
};

}  // namespace sbso::app
