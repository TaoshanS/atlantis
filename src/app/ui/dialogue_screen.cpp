#include "dialogue_screen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "engine/draw_util.h"
#include "engine/shapes.h"

namespace sbso::app {
using engine::Matrix;
using engine::Point;

namespace {

const char* kLeftHeads[] = {"sb_awe", "unhappySmirk", "whisper", "happy", "mesmerized", "bigsmile", "yelling", "worried", "satisfied", "shocked"};
const char* kBeltClips[] = {"chestbelt_greenunderwear", "chestbelt_kelp", "chestbelt_pirate", "chestbelt_shell", "chestbelt_quickster",
                            "chestbelt_ironbelt", "chestbelt_atlantean", "chestbelt_mermaidman", "chestbelt_sardine"};

struct Rgb { int r, g, b; };
const Rgb kInner[11] = {{124, 139, 59}, {147, 55, 67}, {104, 104, 104}, {173, 123, 16}, {77, 134, 137}, {118, 86, 126}, {115, 72, 37}, {173, 123, 16}, {112, 169, 4}, {153, 153, 153}, {255, 255, 255}};
const Rgb kOuter[11] = {{166, 185, 80}, {193, 75, 92}, {154, 154, 154}, {224, 161, 25}, {108, 195, 196}, {152, 109, 159}, {64, 40, 21}, {224, 161, 25}, {81, 125, 1}, {51, 51, 51}, {255, 255, 255}};

int color_index(const std::string& head) {  // DialogueBox._mColorMap (left heads use 0)
    static const std::pair<const char*, int> kMap[] = {
        {"mr_krabs", 1}, {"plankton_terrified", 2}, {"plankton_yelling", 2}, {"plankton_sneaky", 2}, {"plankton_remote", 2}, {"plankton_smiling", 2},
        {"plankton_veryHappy", 2}, {"squidward_talk", 3}, {"squidward_exhausted", 3}, {"squidward_smile", 3}, {"squidward_unhappy", 3}, {"bubblebuddy", 4},
        {"patrick_happy", 5}, {"patrick_wave", 5}, {"patrick_drooling", 5}, {"bully_beach_atlantean", 5}, {"bubble_dirty", 6}, {"pirate_ghost", 7},
        {"atlantean_gaurd_smiling", 8}, {"atlantean_gaurd_angryEyes", 8}, {"atlantean_gaurd_bigSmile", 8}, {"atlantean_king_satisfied", 8},
        {"atlantean_king_talking", 8}, {"manray_forward", 9}, {"manray_backward", 9}};
    for (const auto& p : kMap)
        if (head == p.first) return p.second;
    return 0;  // speakers missing from the map (gaurd_scared, sandy_smile, ...): the AS passes undefined as a uint, i.e. colour 0
}


}  // namespace

DialogueScreen::DialogueScreen(App& app, sbso::game::Dialogue lines, Kind kind, std::string card, int belt, std::function<void()> on_done)
    : app_(app), lines_(std::move(lines)), kind_(kind), card_(std::move(card)), belt_(belt), on_done_(std::move(on_done)) {
    const engine::Library* gui = app.assets().library("GUI");
    if (!gui || lines_.empty()) { phase_ = Phase::Done; return; }
    letter_ = engine::MovieClip::create(gui, gui->symbol_id("Dialogue_LetterBox"), &app);
    letter_->goto_label_and_play("in");
    box_ = engine::MovieClip::create(gui, gui->symbol_id("dialogueBox"), &app);
    box_->stop();
    if (kind_ == Kind::Plain) close_ = engine::MovieClip::create_any(gui, gui->symbol_id("dialoguebutton_close_mc"), &app);
}

bool DialogueScreen::is_left_head(const std::string& h) const {
    for (const char* l : kLeftHeads)
        if (h == l) return true;
    return false;
}

void DialogueScreen::set_head(App& app, const std::string& frame) {
    const engine::Library* heads = app.assets().library("Heads");
    if (!heads) return;
    bool left = is_left_head(frame);
    left_active_ = left;
    auto make = [&](const char* cls) {
        auto c = engine::MovieClip::create(heads, heads->symbol_id(cls), &app);
        if (auto* hs = c->child("cHeads")) {
            hs->goto_label_and_stop(frame);
            if (auto* h = hs->child("cHead")) h->goto_and_play(1);
        }
        return c;
    };
    if (left) { head_l_ = make("LeftHeads"); head_playing_l_ = true; if (head_r_) head_r_.reset(); }
    else { head_r_ = make("RightHeads"); head_playing_r_ = true; if (head_l_) head_l_.reset(); }
    // setUpColorChangers: blend from the previous speaker's colour (white on the first line)
    int ci = left ? 0 : color_index(frame);
    tint_from_ = last_color_ == -1 ? 10 : last_color_;
    tint_to_ = ci;
    last_color_ = ci;
    tint_prog_ = 0;
    tint_active_ = true;
    apply_tint(0);
    // initFGFill + initMove: the bubble starts on the first speaker's side and slides to the new one
    float x = 0, y = 0;
    box_->get_child_xy("cInnerFill", &x, &y);
    if (!mover_ready_) {
        mover_ready_ = true;
        left_x_ = x;
        x = left ? left_x_ : left_x_ - 130;
        box_->set_child_xy("cInnerFill", x, y);
    }
    box_->set_text("cText", "");
    mover_from_ = x;
    mover_to_ = left ? left_x_ : left_x_ - 130;
    mover_prog_ = 0;
    if (mover_from_ == mover_to_) { mover_active_ = false; set_copy(app); }
    else mover_active_ = true;
}

void DialogueScreen::apply_tint(float prog) {
    auto lerp = [&](int a, int b) {
        float t = (1 - std::cos(prog * 3.14159265358979f)) / 2;
        return static_cast<int>(a * (1 - t) + b * t);  // uint() truncation
    };
    box_->set_child_tint("cBGFill", lerp(kOuter[tint_from_].r, kOuter[tint_to_].r), lerp(kOuter[tint_from_].g, kOuter[tint_to_].g), lerp(kOuter[tint_from_].b, kOuter[tint_to_].b));
    box_->set_child_tint("cInnerFill", lerp(kInner[tint_from_].r, kInner[tint_to_].r), lerp(kInner[tint_from_].g, kInner[tint_to_].g), lerp(kInner[tint_from_].b, kInner[tint_to_].b));
}

void DialogueScreen::tick_bubble(App& app) {
    if (tint_active_) {
        tint_prog_ += 0.1666f;
        if (tint_prog_ < 1) apply_tint(tint_prog_);
        else { tint_active_ = false; apply_tint(1); }
    }
    if (mover_active_) {
        mover_prog_ += 0.1666666f;
        float x, y;
        box_->get_child_xy("cInnerFill", &x, &y);
        if (mover_prog_ < 1) {
            float t = (1 - std::cos(mover_prog_ * 3.14159265358979f)) / 2;
            box_->set_child_xy("cInnerFill", mover_from_ * (1 - t) + mover_to_ * t, y);
        } else {
            box_->set_child_xy("cInnerFill", mover_to_, y);
            mover_active_ = false;
            set_copy(app);
        }
    }
}

void DialogueScreen::set_copy(App& app) {
    float x, y, ty, mx, my;
    box_->get_child_xy("cInnerFill", &x, &y);  // BUBBLE_MOVER_DONE: the mask and the text follow the fill
    box_->get_child_xy("cMask", &mx, &my);
    box_->set_child_xy("cMask", x, my);
    box_->get_child_xy("cText", &mx, &ty);
    text_y_ = 78;
    box_->set_child_xy("cText", x + 15, text_y_);
    if (index_ >= lines_.size()) return;
    const auto& l = lines_[index_];
    std::string text = app.tr(l.key, l.text);
    box_->set_text("cText", text);
    // numLines / line pitch come from the real wrapped layout of the text field (not an estimate), so long or translated text scrolls correctly
    box_->set_text_line_height("cText", 25);  // DialogueBox._mHeightOfOneLine: the Flash text field's line pitch
    engine::TextRun run;
    int n = 1;
    line_h_ = 25;
    if (box_->text_run("cText", &run)) {
        auto m = app.assets().text().measure(run);
        n = std::max(1, m.lines);
        if (m.line_height > 1) line_h_ = m.line_height;
    }
    if (std::getenv("SBSO_DEBUG_TEXT")) std::fprintf(stderr, "dialogue text: %d lines, pitch %.2f\n", n, line_h_);
    scroll_lines_ = n <= 3 ? 0 : n - 3;
}

void DialogueScreen::show_line(App& app) {
    if (index_ >= lines_.size()) { finish_lines(app); return; }
    set_head(app, lines_[index_].frame);
}

void DialogueScreen::advance(App& app) {
    if (mover_active_) return;  // the arrow is only live once the text is set
    app.sound().play_sound("button_click", false, -1);
    if (scroll_lines_ > 0) {  // handleArrowClicked: long text scrolls three lines at a time
        scroll_lines_ -= 3;
        text_y_ -= 3 * line_h_;
        float x, y;
        box_->get_child_xy("cText", &x, &y);
        box_->set_child_xy("cText", x, text_y_);
        return;
    }
    if (on_next_) on_next_(static_cast<int>(index_) + 1);
    ++index_;
    show_line(app);
}

void DialogueScreen::finish_lines(App&) {
    if (kind_ == Kind::Plain) {
        phase_ = Phase::LetterOut;
        letter_->goto_label_and_play("out");
    } else if (bg_) {
        phase_ = Phase::BgOutro;  // GainCard/GainBelt: play the background outro first
        // the outro re-places the panel as a fresh copy whose contents start off screen (the picture vanished at once, then the frame
        // dropped): keep the panel's own instance so card and text travel down with it
        bg_->keep_instance("cWinCardBG");
        bg_->play();
    } else {
        phase_ = Phase::LetterOut;
        letter_->goto_label_and_play("out");
    }
}

void DialogueScreen::tick(App& app) {
    if (phase_ == Phase::Done) { if (on_done_) { auto cb = std::move(on_done_); on_done_ = nullptr; app.close_overlay(); cb(); } return; }
    letter_->tick();
    if (phase_ == Phase::LetterIn) {
        if (letter_->current_frame() + 1 >= 13) {  // label "idle"
            letter_->stop();
            phase_ = Phase::Talking;
            show_line(app);
        }
        return;
    }
    if (phase_ == Phase::Talking) {
        box_->tick();
        // DialogueBox.load: both the arrow and the close button stop on their last frame
        if (auto* arrow = box_->child("cArrowMC"))
            if (arrow->current_frame() + 1 >= arrow->total_frames()) arrow->stop();
        if (close_) {
            close_->tick();
            if (close_->current_frame() + 1 >= close_->total_frames()) close_->stop();
        }
        tick_bubble(app);
        for (auto* p : {&head_l_, &head_r_}) {
            if (!*p) continue;
            (*p)->tick();
            if (auto* hs = (*p)->child("cHeads"))
                if (auto* h = hs->child("cHead"))
                    if (h->frame_of_label("talk") >= 0 && h->current_frame() >= h->frame_of_label("talk")) h->stop();
        }
        if (kind_ != Kind::Plain && ++bg_counter_ == 3) {  // monitorWhenToShow: the background appears on the 3rd frame
            const engine::Library* lib = nullptr;
            if (kind_ == Kind::GainCard) {
                lib = app.assets().library("GUI");
                if (lib) bg_ = engine::MovieClip::create(lib, lib->symbol_id("GainCardDialogueMC"), &app);
                app.sound().play_sound("battle_new_card_earned", false, -1);
            } else {
                lib = app.assets().library("newBelt");
                if (lib) bg_ = engine::MovieClip::create(lib, lib->symbol_id("BeltDialogueBGAnim"), &app);
                app.sound().play_sound("sb_belt_win", false, -1);
            }
        }
        if (bg_) {
            bg_->tick();
            if (bg_->current_frame() == 4 && kind_ == Kind::GainCard) bg_->stop();  // monitorIntro: hold on frame 5 then play the card BG
        }
        return;
    }
    if (phase_ == Phase::BgOutro) {
        if (bg_) {
            bg_->play();
            bg_->tick();
            if (bg_->current_frame() >= bg_->total_frames() - 1) { phase_ = Phase::LetterOut; letter_->goto_label_and_play("out"); }
        }
        return;
    }
    if (phase_ == Phase::LetterOut) {
        if (letter_->current_frame() >= letter_->total_frames() - 1) phase_ = Phase::Done;
    }
}

Matrix DialogueScreen::close_matrix() const {  // the CLOSE tab over the box; phones draw (and hit-test) it bigger around its centre
    Matrix m = place_ * Matrix{1, 0, 0, 1, -30, 55 - 15};
    if (!kMobile || !close_) return m;
    engine::Rect b = close_->bounds();
    const float k = kMobileCloseScale, cx = (b.x0 + b.x1) / 2, cy = (b.y0 + b.y1) / 2;
    return m * Matrix{k, 0, 0, k, cx - k * cx, cy - k * cy};
}

void DialogueScreen::pointer_move(App&, Point p) { pointer_ = p; }

void DialogueScreen::pointer_down(App&, Point p) {
    pointer_ = p;
    if (phase_ != Phase::Talking) return;
    pointer_down_on_close_ = false;
    if (close_) {
        std::vector<engine::ButtonHit> hits;
        close_->collect_buttons(hits, close_matrix());
        for (auto& h : hits) if (h.contains(p)) pointer_down_on_close_ = true;
    }
    // port improvement (touch screens): a tap anywhere but CLOSE does what the arrow does; the original only took the arrow
    pointer_down_on_arrow_ = !pointer_down_on_close_;
}

void DialogueScreen::pointer_up(App& app, Point p) {
    pointer_ = p;
    if (phase_ != Phase::Talking) return;
    if (pointer_down_on_arrow_) { pointer_down_on_arrow_ = false; advance(app); return; }
    if (pointer_down_on_close_) {
        std::vector<engine::ButtonHit> hits;
        close_->collect_buttons(hits, close_matrix());
        for (auto& h : hits)
            if (h.contains(p)) { app.sound().play_sound("button_click", false, -1); finish_lines(app); return; }
    }
    pointer_down_on_arrow_ = pointer_down_on_close_ = false;
}

void DialogueScreen::key_down(App& app, int key) {
    if (phase_ == Phase::Talking && (key == 13 || key == 32)) advance(app);  // Enter / Space
}

void DialogueScreen::draw(App& app, std::vector<engine::DrawCmd>& out) {
    if (phase_ == Phase::Done || !letter_) return;
    // On a wider scene the letterbox bars are repeated sideways and everything else is clipped to the original 640x480 stage:
    // the panel, heads and card/belt backgrounds were authored to be cut by the stage edge.
    const float extra = (app.layout().logical_w - sbso::kDesignW) / 2.0f;
    if (extra > 0) {
        // The letterbox (a flat dimming layer and two bars) is stretched sideways over the whole scene, with a little overlap past the
        // window edges: side-by-side copies always left a seam where the edges of their bitmaps meet.
        const float cx = sbso::kDesignW / 2.0f, f = (sbso::kDesignW + 2 * extra + 16) / sbso::kDesignW;
        letter_->collect(out, Matrix{f, 0, 0, 1, cx - f * cx, 0} * place_, engine::ColorTransform{});
    } else {
        letter_->collect(out, place_, engine::ColorTransform{});
    }
    if (phase_ == Phase::LetterIn || phase_ == Phase::LetterOut) return;
    static engine::SolidPath stage;
    if (extra > 0) {
        stage = engine::round_rect(0, 0, sbso::kDesignW, sbso::kDesignH, 0, 0, 0, 0, 1.0f);
        out.push_back(engine::DrawCmd(engine::DrawCmd::Kind::MaskBegin));
        engine::DrawCmd d;
        d.kind = engine::DrawCmd::Kind::Solid;
        d.solid = &stage;
        out.push_back(d);
        out.push_back(engine::DrawCmd(engine::DrawCmd::Kind::MaskApply));
    }
    if (bg_) {
        Matrix m = place_;
        bg_->collect(out, m, engine::ColorTransform{});
        if (kind_ == Kind::GainCard) {
            Matrix pm;
            if (bg_->path_matrix("cWinCardBG.cCardPlaceholder", &pm)) {
                const auto* info = app_.assets().db().card(card_);
                const engine::Library* cards = app_.assets().library("cardAssets");
                if (info && cards) {
                    int img = cards->symbol_id("cards." + info->symbol);
                    engine::DrawCmd d;
                    if (img >= 0 && engine::image_cmd(cards, img, m * pm, &d)) out.push_back(d);
                }
            }
        } else if (kind_ == Kind::GainBelt) {
            Matrix pm;
            if (bg_->path_matrix("cBD.cBG.cBeltPlaceholder", &pm)) {
                int idx = std::max(0, std::min(8, belt_ - 1));
                int id = -1;
                if (const engine::Library* lib = app_.assets().find_class(kBeltClips[idx], &id)) {
                    auto belt = engine::MovieClip::create_any(lib, id, &app_);
                    belt->collect(out, m * pm * Matrix{1, 0, 0, 1, 50, 20}, engine::ColorTransform{});
                }
            }
        }
    }
    if (phase_ == Phase::Talking || phase_ == Phase::BgOutro) {
        box_->collect(out, place_, engine::ColorTransform{});
        if (close_) close_->collect(out, close_matrix(), engine::ColorTransform{});
        if (head_l_) head_l_->collect(out, place_, engine::ColorTransform{});
        if (head_r_) head_r_->collect(out, place_, engine::ColorTransform{});
    }
    if (extra > 0) out.push_back(engine::DrawCmd(engine::DrawCmd::Kind::MaskEnd));
}

}  // namespace sbso::app
