// Port of MinigameScreen.as + MinigameCardGrid.as + Card_Minigame.as: Bubble Buddy's card game (pay coins, shuffle, pick).
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/app.h"
#include "app/ui/clip_panel.h"

namespace sbso::app {

class MinigameScreen : public Screen {
public:
    MinigameScreen(App& app, int game /*0..3*/, std::function<void()> on_done);
    ~MinigameScreen() override;
    void tick(App& app) override;
    void draw(App& app, std::vector<engine::DrawCmd>& out) override;
    void pointer_move(App& app, engine::Point p) override;
    void pointer_down(App& app, engine::Point p) override;
    void pointer_up(App& app, engine::Point p) override;

    // Test hooks.
    bool waiting_for_choice() const { return phase_ == Phase::Choice; }
    void press_yes() { yes_clicked(); }
    bool cards_ready() const;
    void pick(int index);
    int num_cards() const;

private:
    enum class Phase { Entry, Choice, LoadCards, Playing, CardEarned, PlayAgain, ExitForwards, ExitBackwards, TooPoorForwards, TooPoorBackwards, Done };
    struct Card;
    struct Grid;

    void finish(App& app);
    void tick_grid(App& app);
    void init_yes_no(bool on);
    void yes_clicked();
    void no_clicked();
    void show_line(int index);
    void set_head(const std::string& frame, const std::string& text);
    bool has_enough_coins() const;
    void show_too_poor();
    void load_cards();
    void card_chosen(Card& c);
    int phase_label_frame(const char* label) const;

    App& app_;
    int game_;
    std::function<void()> on_done_;
    ClipPanel panel_;
    Phase phase_ = Phase::Entry;
    bool disabled_ = true, buttons_on_ = false;
    int timer_ = 0;
    std::unique_ptr<engine::MovieClip> dialogue_;
    std::unique_ptr<engine::MovieClip> head_l_, head_r_;
    std::unique_ptr<Grid> grid_;
    std::string chosen_;
    int lines_shown_ = 0;
    engine::Point pointer_{-1000, -1000};
    bool done_called_ = false;
};

}  // namespace sbso::app
