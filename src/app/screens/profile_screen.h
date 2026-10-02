// Port of ProfileScreen.as + ProfileWidget(RadioSet): "Who are you?" with up to three profiles, rename / delete / new.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/app.h"
#include "app/ui/skin.h"
#include "app/ui/clip_panel.h"

namespace sbso::app {

class ProfileScreen : public Screen {
public:
    // on_changed: PROFILE_CHANGED (the title refreshes its welcome text); on_closed: PROFILE_SCREEN_CLOSED.
    ProfileScreen(App& app, std::function<void()> on_changed, std::function<void()> on_closed);
    void tick(App& app) override;
    void draw(App& app, std::vector<engine::DrawCmd>& out) override;
    void pointer_move(App& app, engine::Point p) override;
    void pointer_down(App& app, engine::Point p) override;
    void pointer_up(App& app, engine::Point p) override;
    void key_down(App& app, int key) override;
    void text_input(App& app, const std::string& utf8) override;
    ~ProfileScreen() override;

    enum class State { View, Rename, Delete };
    State state() const { return state_; }
    const std::string& input() const { return input_; }

private:
    struct Widget {
        ClipPanel panel;
        bool is_new = true;
        int index = 0;
        engine::Rect bounds;  // in screen space
    };
    void change_state(State s);
    void build_view();
    void build_rename();
    void update_buttons();
    void check_uniqueness();
    void move_brackets(int index, bool sound);
    void darken(int index);
    void close(App& app);
    void apply_rename(App& app);
    void update_cursor();
    bool ok_enabled_ = false, close_enabled_ = false;
    engine::Rect saves_button() const;
    bool over_saves(const App& app, engine::Point p) const;
    engine::Rect saves_rect_;
    Skin skin_;
    engine::Point pointer_{-1000, -1000};

    App& app_;
    std::function<void()> on_changed_, on_closed_;
    const engine::Library* lib_ = nullptr;
    State state_ = State::View;
    ClipPanel bg_;
    std::vector<Widget> widgets_;
    std::unique_ptr<engine::MovieClip> brackets_;
    int bracket_index_ = -1, selected_ = -1;
    engine::Matrix place_;
    // rename state
    std::string input_;
    bool is_new_ = false;
    int blink_ = 0;
    bool cursor_visible_ = true;
    engine::SolidPath cursor_shape_;
    engine::Point cursor_pos_;
};

}  // namespace sbso::app
