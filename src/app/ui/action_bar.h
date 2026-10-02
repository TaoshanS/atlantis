// Port of ActionGUI.as + CardCarousel.as + Card_InGame.as: the card hand shown at the bottom during the player's turn.
#pragma once
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/app.h"
#include "game/sim.h"

namespace sbso::app {

class ActionBar {
public:
    struct Callbacks {
        std::function<void(const std::string& card, bool clicked)> card_selected;  // hover (clicked=false) or click
        std::function<void()> card_released;                                        // pointer left the hovered card
        std::function<void()> end_turn;
    };

    ActionBar(App& app, int capacity, Callbacks cb);
    void tick();
    void draw(std::vector<engine::DrawCmd>& out);

    // Mirrors the simulation: used cards flip away, unaffordable ones are dimmed.
    void refresh(const sbso::game::Sim& sim);
    void show(bool on) { target_y_ = on ? on_y_ : off_y_; }
    bool visible() const { return y_ < off_y_ - 1; }
    void set_enabled(bool e);
    void set_selected(const std::string& card);  // raises the card (setLoc(3))
    void show_type(sbso::game::CardType t);
    void cycle_type(bool right) { if (built_ && enabled_) carousel_press(right); }  // the carousel arrows (swipe gesture)
    sbso::game::CardType current_type() const { return type_; }
    void set_glow(bool on) { glow_ = on; }

    void pointer_move(engine::Point p);
    bool pointer_down(engine::Point p);  // true if consumed
    bool pointer_up(engine::Point p);

private:
    struct CardView {
        std::string name;
        sbso::game::CardType type = sbso::game::CardType::Move;
        int image = -1;
        std::string cost, power;
        std::unique_ptr<engine::MovieClip> flip;
        bool used = false, dim = false;
        int loc = 1;  // 1 rest, 2 hover (-5), 3 selected (-10)
    };
    struct Carousel {
        std::unique_ptr<engine::MovieClip> clips[4];  // NICK, MOVE, ATTACK, DEFENSE
        std::unique_ptr<engine::MovieClip> arrows;
        double angle = 0, target = 0, force = 0;
        int counter = 0, num_updates = 16, current = 0;
        bool turning_left = false, turning_right = false;
        float x = 0, y = 0;
        engine::Rect left_rect, right_rect;  // in carousel space
    };

    void layout();
    void set_angle(double a);
    void carousel_press(bool right);
    int type_index(sbso::game::CardType t) const { return static_cast<int>(t); }
    CardView* hit_card(engine::Point local);
    engine::Rect card_rect(const CardView& c, int slot) const;
    void set_loc(CardView& c, int loc) { c.loc = loc; }

    App& app_;
    Callbacks cb_;
    int capacity_;
    const engine::Library* holder_lib_ = nullptr;
    const engine::Library* card_lib_ = nullptr;
    std::unique_ptr<engine::MovieClip> bg_, end_button_, glow_clip_;
    std::vector<std::unique_ptr<engine::MovieClip>> slots_;
    float slot_w_ = 70, slot_h_ = 90, bg_w_ = 600, bg_h_ = 100;
    float x_ = 0, y_ = 0, on_y_ = 0, off_y_ = 0, target_y_ = 0;
    std::array<std::vector<CardView>, 4> cards_;
    sbso::game::CardType type_ = sbso::game::CardType::Move;
    Carousel car_;
    bool enabled_ = true, glow_ = false;
    std::string selected_;
    engine::Point pointer_{-1000, -1000};
    CardView* hover_ = nullptr;
    bool end_down_ = false, end_hover_ = false;
    bool built_ = false;
};

}  // namespace sbso::app
