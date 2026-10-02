// Port of GameLevel.as / Map.as presentation: a battle level with HUD, card hand and animations.
// The rules live in game::Sim; this class turns its events into visuals and player input into Sim calls.
#pragma once
#include <deque>
#include <functional>
#include <random>
#include <map>
#include <memory>

#include "app/app.h"
#include "app/ui/action_bar.h"
#include "app/ui/battle_animator.h"
#include "app/ui/dialogue_screen.h"
#include "app/ui/clip_panel.h"
#include "app/ui/hud.h"
#include "engine/level_view.h"
#include "game/sim.h"

namespace sbso::app {

class BattleScreen : public Screen {
public:
    // `resume`: a Sim::snapshot() of this very level (mid-level save); the battle then continues from that turn.
    BattleScreen(App& app, int stage, int level, std::uint64_t seed, const std::vector<std::uint8_t>* resume = nullptr);
    void tick(App& app) override;
    void draw(App& app, std::vector<engine::DrawCmd>& out) override;
    void pointer_move(App& app, engine::Point p) override;
    void pointer_down(App& app, engine::Point p) override;
    void pointer_up(App& app, engine::Point p) override;
    void key_down(App& app, int key) override;
    void on_suspend(App& app) override;
    // stage.frameRate as GameLevel / BattleManager keep switching it: 38 normally, 31 during battle animations and after dialogues.
    float tick_rate() const override { return rate_; }

    // How the level was left: GameLevel dispatches LEVEL_COMPLETE / GAME_OVER, the in-game menu EXIT_TO_TITLESCREEN.
    enum class Exit { Quit, Victory, Defeat };
    void set_exit_handler(std::function<void(Exit)> h) { exit_ = std::move(h); }

    bool ok() const { return sim_ != nullptr; }
    const sbso::game::Sim* sim() const { return sim_.get(); }
    // Test hooks: act without going through the pointer.
    bool play_card_at(const std::string& card, sbso::game::Pt tile);
    void press_end_turn();
    void debug_end_level(bool victory);  // test hook: ends the level as if the last enemy fell / SpongeBob was beaten
    bool idle() const { return !busy_ && queue_.empty() && !banner_; }

private:
    using Event = sbso::game::Event;
    void pull_events();
    void process_events();
    bool begin_event(const Event& e);  // true if it starts a blocking animation
    void begin_move(int unit, const std::vector<sbso::game::Pt>& path);
    void tick_move();
    void scroll_to(float x, float y);
    void follow_spongebob();
    void center_on_tile(int tx, int ty);
    void select_card(const std::string& card, bool clicked);
    void release_card();
    void update_highlights();
    void refresh_hud();
    void place_menu_buttons();
    void show_help(bool forced);
    void open_menu();
    void menu_choice(const std::string& what);
    void show_ui(bool on);
    void leave(Exit how);
    void take_turn_snapshot();
    void open_dialogue(const sbso::game::Dialogue& d, DialogueScreen::Kind kind, const std::string& card, int belt, std::function<void()> then = nullptr);
    engine::Point to_map(engine::Point p) const { return {(p.x - map_x_) / zoom_, (p.y - map_y_) / zoom_}; }
    void update_zoom();                     // the map is magnified when the viewport is wider than the level
    sbso::game::Pt tile_at(engine::Point p) const;
    int logical_width(App& app) const;
    float safe_left() const;
    engine::Matrix bar_matrix() const;
    engine::Point to_bar(engine::Point p) const;
    float safe_right() const;
public:
    bool uses_wide_layout() const override { return true; }
    float content_zoom() const override { return zoom_ > 0 ? zoom_ : 1.0f; }
private:

    App& app_;
    engine::Point swipe_from_{};
    int swipe_ticks_ = 0;
    bool swiping_ = false;
    int stage_, level_num_;
    sbso::game::Level level_;
    std::unique_ptr<sbso::game::Sim> sim_;
    engine::LevelView view_;
    std::unique_ptr<Hud> hud_;
    std::unique_ptr<ActionBar> bar_;
    const engine::Library* xtras_ = nullptr;
    const engine::Library* gui_ = nullptr;
    const engine::Library* bubbles_ = nullptr;

    std::vector<engine::LevelView::UnitVisual> vis_;
    std::deque<Event> queue_;
    float map_x_ = 0, map_y_ = 0;
    float zoom_ = 0;                        // screen px per map px (>= 1); map_w_/map_h_ and map_x_/map_y_ are in screen px
    float map_w_ = 0, map_h_ = 0;
    int viewport_w_ = sbso::kDesignW;

    // selection / highlights
    std::string selected_;
    std::string preview_;
    std::vector<sbso::game::Pt> highlights_;
    sbso::game::CardType highlight_type_ = sbso::game::CardType::Move;
    std::map<int, std::unique_ptr<engine::MovieClip>> highlight_clips_;
    std::unique_ptr<engine::MovieClip> cursor_;
    sbso::game::Pt cursor_tile_{-1, -1};
    engine::Point pointer_{-1000, -1000};

    // animations
    bool hide_enemy_after_ = false;
    int hover_enemy_ = -1;
    void update_hover_enemy();
    bool busy_ = false;
    int wait_ = 0;
    struct MoveAnim { int unit = -1; std::vector<sbso::game::Pt> path; size_t next = 0; } move_;
    std::unique_ptr<BattleAnimator> animator_;
    bool counter_seen_ = false;  // BattleManager._mCounterAttackCard != "": skips the floor intro
    std::unique_ptr<engine::MovieClip> banner_;
    bool banner_player_ = true;
    std::vector<std::pair<std::unique_ptr<engine::MovieClip>, engine::Point>> effects_;
    std::unique_ptr<engine::MovieClip> end_anim_;
    bool end_victory_ = false;
    bool finished_ = false;
    ClipPanel menu_buttons_;  // inGameMenuButtons: menu / help / hide-show HUD
    bool ui_hidden_ = false;
    float rate_ = 38.0f;
    std::vector<std::uint8_t> turn_snapshot_;  // Sim state at the start of the player's current turn
    std::function<void(Exit)> exit_;
    bool started_ = false, cutscene_seen_ = false;
    // Map.introAnimation: the map first shows the flag (when it is off screen), waits, then scrolls to SpongeBob
    int intro_state_ = 0, intro_timer_ = 0;  // 0 none, 1 waiting, 2 scrolling
    engine::Point intro_from_, intro_to_;
    float intro_prog_ = 0, intro_inc_ = 1;  // intro dialogue shown
    std::mt19937 cosmetic_rng_{std::random_device{}()};
};

}  // namespace sbso::app
