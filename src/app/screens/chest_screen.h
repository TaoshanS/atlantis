// Port of TreasureChest.as + Chest_BattleDeckScreen.as + Card_BattleDeck.as + Chest_CoinScreen.as:
// the treasure chest with its four tabs (cards / belts / coins / flags) where the battle deck is edited.
#pragma once
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/app.h"
#include "app/ui/card_carousel.h"
#include "app/ui/clip_panel.h"

namespace sbso::app {

class ChestScreen : public Screen {
public:
    explicit ChestScreen(App& app);
    ~ChestScreen() override;
    void tick(App& app) override;
    void draw(App& app, std::vector<engine::DrawCmd>& out) override;
    void pointer_move(App& app, engine::Point p) override;
    void pointer_down(App& app, engine::Point p) override;
    void pointer_up(App& app, engine::Point p) override;
    void key_down(App& app, int key) override;
    float tick_rate() const override { return 38.0f; }

    enum class Tab { Cards, Belts, Coins, Flags };
    Tab tab() const { return tab_; }
    // Test hooks.
    void press(const std::string& button) { activate(button); }
    bool click_card(int type, const std::string& name);
    bool belt_has_min_requirements();

private:
    struct DeckCard;
    struct Stack { float x, y; int coins; };

    void set_tab(Tab t);
    void enter_tab();
    void exit_tab();
    void tab_update();
    void activate(const std::string& button);
    void show_help();
    void done_clicked();
    void exit_clicked();
    void show_warning();
    void open_menu();

    // cards tab (Chest_BattleDeckScreen)
    void create_cards();
    void set_card_type(int type);
    void change_card_type(int type, bool from_carousel);
    void scroll_grid();
    int max_row_offset() const;
    void set_grid_scroll(int offset);
    void update_scroll_bar();
    void set_num_cards();
    void show_range(int type, const std::string& card);
    void hide_range();
    void card_roll_over(DeckCard& c);
    void card_roll_out(DeckCard& c);
    void card_click(DeckCard& c);
    void tick_card(DeckCard& c);
    void move_in(DeckCard& c, bool from_top, int index);
    engine::Point belt_holder_pos(int i) const;
    engine::Point grid_holder_pos(int i) const;
    DeckCard* card_at(engine::Point p);
    void remove_extra_cards_from_belt();

    // belts tab
    void build_belt_tab();
    void change_belt_display(int belt, bool temp);
    void belt_clicked(int i);

    // coins / flags tabs
    void build_coin_stacks();
    void update_map_flags();

    App& app_;
    const engine::Library* lib_ = nullptr;
    ClipPanel panel_, menu_buttons_;
    engine::Matrix place_;
    Tab tab_ = Tab::Cards;
    int timer_ = 0;
    bool buttons_live_ = false, enabled_ = false, has_enough_cards_ = true;
    int current_belt_ = 0, belt_shown_ = 0;
    bool intro_done_ = false;
    int intro_counter_ = 0;

    // cards tab state
    std::array<std::vector<std::unique_ptr<DeckCard>>, 4> all_cards_;
    std::array<std::vector<int>, 4> belt_slots_;
    std::vector<DeckCard*> display_;
    int card_type_ = -1;
    int row_offset_ = 0;
    int num_cards_ = 3;
    std::unique_ptr<CardCarousel> carousel_;
    int carousel_timer_ = 0;
    std::unique_ptr<engine::MovieClip> radio_, slot_clip_[5], grid_slot_;
    std::vector<std::unique_ptr<engine::MovieClip>> belt_holders_;
    std::vector<std::unique_ptr<engine::MovieClip>> range_tiles_;
    std::vector<engine::Point> range_pos_;
    std::vector<int> range_type_;
    std::vector<std::unique_ptr<engine::MovieClip>> range_clips_;
    bool range_visible_ = false;
    DeckCard* hovered_ = nullptr;
    engine::Point pointer_{-1000, -1000};
    bool dragging_bar_ = false;
    std::array<std::vector<std::string>, 4> new_snapshot_;
    bool viewed_[4] = {false, false, false, false};
    const engine::Library* card_lib_ = nullptr;
    const engine::Library* holder_lib_ = nullptr;

    // belts tab state
    std::vector<engine::MovieClip*> belt_buttons_;       // owned by the cBelt_N clips
    std::vector<engine::MovieClip*> belt_shadows_;
    int belt_hover_ = -1;

    // coins tab state
    std::vector<Stack> stacks_;
    const engine::Library* gui_lib_ = nullptr;
    std::unique_ptr<engine::MovieClip> coin_;
};

}  // namespace sbso::app
