// Port of LevelSelectScreen.as: the world map with its unfolding scroll, flags, tour bus, play / chest / arrow buttons.
#pragma once
#include <memory>
#include <vector>

#include "app/app.h"
#include "app/ui/clip_panel.h"
#include "app/ui/world_map.h"

namespace sbso::app {

class LevelSelectScreen : public Screen, public StageMapHost {
public:
    explicit LevelSelectScreen(App& app);
    ~LevelSelectScreen() override;
    void tick(App& app) override;
    void draw(App& app, std::vector<engine::DrawCmd>& out) override;
    void pointer_move(App& app, engine::Point p) override;
    void pointer_down(App& app, engine::Point p) override;
    void pointer_up(App& app, engine::Point p) override;
    void key_down(App& app, int key) override;
    float tick_rate() const override;  // Settings::map_fps (default 24; the original stage ran at 38 and felt too fast on the map)

    // StageMapHost
    void flag_clicked() override { enable(false); }
    void tour_bus_done() override { enable(true); }
    void stage_complete() override;
    void check_complete_all_stages() override;
    void unlock_ani_done() override;

    // Test hooks.
    int showing_stage() const { return showing_; }
    bool enabled() const { return enabled_; }
    void press(const std::string& button);  // "cPlayButt", "cLeftButton", ...

private:
    void enable(bool on) { enabled_ = on; }
    void load();
    void get_new_map(int stage);
    void handle_scroll(int stage);
    void show_map(int delta);
    void scroll_done();
    void check_to_hide_arrows();
    void handle_clicked(const std::string& button);
    void handle_play();
    void open_menu();
    void menu_choice(const std::string& what);
    engine::Point to_map(engine::Point p) const;
    StageMap* current_map() { return maps_[showing_ - 1].get(); }

    App& app_;
    const engine::Library* gui_ = nullptr;
    ClipPanel bg_, menu_buttons_;
    std::unique_ptr<engine::MovieClip> all_levels_, scrollers_;
    std::unique_ptr<StageMap> maps_[9];
    StageMap* attached_ = nullptr;
    engine::Matrix place_;
    int showing_ = 1;
    bool loaded_ = false, enabled_ = true, chest_available_ = false, chest_added_ = false;
    bool intro_done_ = false, play_enabled_ = true;
    bool scrolling_ = false;
    std::string scroll_start_label_ = "entry";
    int scroll_sound_ = -1;
    bool wait_unlock_ = false;
    bool complete_wait_ = false;
    int complete_timer_ = 0;
};

}  // namespace sbso::app
