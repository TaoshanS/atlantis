// Port of HUD.as: SpongeBob / enemy status panels (bars and three-digit counters).
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "app/app.h"

namespace sbso::app {

class Hud {
public:
    explicit Hud(App& app);
    void tick();
    void draw(std::vector<engine::DrawCmd>& out, const engine::Matrix& m);

    void show_player(int health, int energy, int health_pct, int energy_pct, int coins);
    void set_player_health(int health, int pct);
    void set_energy(int value, int pct, bool player);
    bool has_face(const std::string& type);  // the enemy panel has a portrait for this unit type
    void show_enemy(const std::string& type, int health, int energy, int health_pct, int energy_pct);
    void set_enemy_health(int health, int pct);
    void hide_player();
    void hide_enemy();
    void set_coins(int coins);
    bool animations_done() const;  // all bars reached their target

private:
    struct Panel {
        std::unique_ptr<engine::MovieClip> clip;
        bool visible = false;
        int target_health = 100, target_energy = 100;
        double health_inc = 1, energy_inc = 1;  // Flash's incVal easing state
    };
    void three_digits(engine::MovieClip* holder, int value);
    void ease_bar(engine::MovieClip* bar, int target, double& inc);
    void bar_to(Panel& p, const char* bar, int pct, bool animate);
    void hold_intro(Panel& p);

    App& app_;
    const engine::Library* lib_ = nullptr;
    Panel sb_, enemy_;
};

}  // namespace sbso::app
