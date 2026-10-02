// Port of DialogueScreen/DialogueBox/DialogueLetterBox/GainCard-/GainBeltDialogue (simplified head/colour transitions).
#pragma once
#include <functional>
#include <memory>
#include <vector>

#include "app/app.h"
#include "game/dialogues.h"

namespace sbso::app {

class DialogueScreen : public Screen {
public:
    enum class Kind { Plain, GainCard, GainBelt };
    // `lines` are copied; `card` is used for GainCard (name of the card shown); `belt` index for GainBelt.
    DialogueScreen(App& app, sbso::game::Dialogue lines, Kind kind, std::string card, int belt, std::function<void()> on_done);
    void tick(App& app) override;
    void draw(App& app, std::vector<engine::DrawCmd>& out) override;
    void pointer_move(App& app, engine::Point p) override;
    void pointer_down(App& app, engine::Point p) override;
    void pointer_up(App& app, engine::Point p) override;
    void key_down(App& app, int key) override;
    // DIALOGUE_NEXT: fired on every arrow click with the number of lines shown so far (DialogueScreen.getPage).
    void set_on_next(std::function<void(int)> f) { on_next_ = std::move(f); }
    float tick_rate() const override { return 31.0f; }

private:
    enum class Phase { LetterIn, Talking, BgOutro, LetterOut, Done };
    void show_line(App& app);
    void advance(App& app);
    engine::Matrix close_matrix() const;
    void finish_lines(App& app);
    bool is_left_head(const std::string& h) const;
    void set_head(App& app, const std::string& frame);
    void set_copy(App& app);              // DialogueBox.copy: runs when the bubble reaches its side (SET_COPY)
    void tick_bubble(App& app);           // DialogueBubbleMover + the two ColorChangers
    void apply_tint(float prog);

    App& app_;
    sbso::game::Dialogue lines_;
    Kind kind_;
    std::string card_;
    int belt_;
    std::function<void()> on_done_;
    Phase phase_ = Phase::LetterIn;
    size_t index_ = 0;
    std::function<void(int)> on_next_;
    int scroll_lines_ = 0;
    int bg_counter_ = 0;
    float text_y_ = 78;
    float line_h_ = 25;   // pitch of the dialogue text, measured per line (DialogueBox._mHeightOfOneLine = 25 in the original)
    std::unique_ptr<engine::MovieClip> letter_, box_, close_, head_l_, head_r_, bg_;
    bool head_playing_l_ = false, head_playing_r_ = false;
    bool pointer_down_on_arrow_ = false, pointer_down_on_close_ = false;
    engine::Point pointer_{-1000, -1000};
    engine::Matrix place_{1, 0, 0, 1, 320, 240};
    int closing_wait_ = 0;
    bool left_active_ = true;
    // DialogueBubbleMover: the bubble slides 130 px between the heads' sides in 6 frames (cosine); the text appears when it arrives.
    bool mover_ready_ = false, mover_active_ = false;
    float mover_prog_ = 0, mover_from_ = 0, mover_to_ = 0, left_x_ = -103;
    // ColorChanger pair (outline + fill): cosine blend from the previous speaker's colours.
    int last_color_ = -1, tint_from_ = 10, tint_to_ = 10;
    bool tint_active_ = false;
    float tint_prog_ = 0;
};

}  // namespace sbso::app
