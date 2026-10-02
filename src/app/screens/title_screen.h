// Port of TitleScreen.as: splash sequence (without the Big Fish Games logo), loader bar and main menu.
#pragma once
#include <memory>

#include "app/app.h"
#include "app/ui/skin.h"

namespace sbso::app {

class TitleScreen : public Screen {
public:
    // `skip_to_menu`: returning from the game (TitleScreen with queue == null jumps straight to the menu).
    TitleScreen(App& app, bool skip_to_menu);
    void tick(App& app) override;
    void draw(App& app, std::vector<engine::DrawCmd>& out) override;
    void pointer_move(App& app, engine::Point p) override;
    void pointer_down(App& app, engine::Point p) override;
    void pointer_up(App& app, engine::Point p) override;
    void key_down(App& app, int key) override;
    float tick_rate() const override { return 32.0f; }

    // State exposed for tests.
    int frame() const { return clip_ ? clip_->current_frame() + 1 : 0; }
    bool menu_ready() const { return phase_ == Phase::Menu; }
    const std::string& last_action() const { return last_action_; }

private:
    enum class Phase { Preload, Splash, Menu };
    void init_buttons(App& app);
    void open_profile_screen(App& app);
    void skip_splash();
    void update_hover(App& app);
    std::string welcome_message(App& app);
    void handle_click(App& app, const std::string& button);

    const engine::Library* lib_ = nullptr;
    std::unique_ptr<engine::MovieClip> clip_;
    Phase phase_ = Phase::Preload;
    int preload_wait_ = 0;
    engine::Point pointer_{-1000, -1000};
    bool pointer_down_ = false;
    std::string hover_name_, down_name_;
    std::string last_action_;
    bool keep_big_fish_ = false;
    engine::Matrix place_;
    // Phones: LOAD (the most recent save) takes the place of QUIT, which apps do not have there.
    Skin skin_;
    engine::Rect load_rect_{};
    bool load_down_ = false;
    engine::Rect quit_rect();
    int latest_slot(App& app);
    void load_latest(App& app);
};

}  // namespace sbso::app
