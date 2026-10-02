#include "action_bar.h"

#include <cmath>

#include "engine/draw_util.h"

namespace sbso::app {
using engine::Matrix;
using engine::Point;
using engine::Rect;
using sbso::game::CardType;

namespace {

constexpr float kPi = 3.14159265f;
const char* kCarouselOrder[4] = {"NICK", "MOVE", "ATTACK", "DEFENSE"};  // carousel clip i

bool inside(const Rect& r, Point p) { return p.x >= r.x0 && p.x < r.x1 && p.y >= r.y0 && p.y < r.y1; }

CardType type_from_carousel(int idx) {  // GameInfo.sCardTypes = MOVE, ATTACK, DEFEND, NICK
    return static_cast<CardType>(idx);
}

}  // namespace

ActionBar::ActionBar(App& app, int capacity, Callbacks cb) : app_(app), cb_(std::move(cb)), capacity_(capacity) {
    holder_lib_ = app.assets().library("cardHolder");
    card_lib_ = app.assets().library("cardAssets");
    if (!holder_lib_ || !card_lib_) return;
    auto cls = [&](const char* n) { return holder_lib_->symbol_id(n); };
    bg_ = engine::MovieClip::create(holder_lib_, cls("CardHolder.Background"), &app);
    Rect bb = bg_->bounds();
    bg_w_ = bb.w();
    bg_h_ = bb.h();
    auto slot = engine::MovieClip::create(holder_lib_, cls("CardHolder.CardSlot"), &app);
    Rect sb = slot->bounds();
    slot_w_ = sb.w();
    slot_h_ = sb.h();
    for (int i = 0; i < 5; ++i) slots_.push_back(engine::MovieClip::create(holder_lib_, cls("CardHolder.CardSlot"), &app));
    for (int i = 0; i < 4; ++i) car_.clips[i] = engine::MovieClip::create(holder_lib_, cls((std::string("CardHolder.Carousel.") + kCarouselOrder[i]).c_str()), &app);
    car_.arrows = engine::MovieClip::create(holder_lib_, cls("CardHolder.Carousel.ArrowButtons"), &app);
    car_.arrows->stop();
    for (const char* n : {"left", "right"})
        if (engine::MovieClip* b = car_.arrows->child(n)) b->stop();
    end_button_ = engine::MovieClip::create_any(holder_lib_, cls("CardHolder.but_ENDTURN"), &app);
    glow_clip_ = engine::MovieClip::create(holder_lib_, cls("CardHolder.but_ENDTURN_glow"), &app);
    x_ = static_cast<float>(static_cast<int>(sbso::kDesignW / 2.0f - bg_w_ / 2.0f));
    off_y_ = sbso::kDesignH + 10.0f;
    on_y_ = static_cast<float>(static_cast<int>(sbso::kDesignH - bg_h_));
    y_ = target_y_ = off_y_;
    layout();
    set_angle(0);
    built_ = true;
}

void ActionBar::layout() {
    // ActionGUI.setNumCards
    float base = static_cast<float>(static_cast<int>(bg_w_ / 2.0f - slot_w_ * capacity_ / 2.0f));
    car_.x = static_cast<float>(static_cast<int>(base + 60.0f / 2.0f - 108.0f));  // _mCarousel.width/2 ~ 30
    car_.y = static_cast<float>(static_cast<int>(bg_h_ / 2.0f));
    Rect eb = end_button_ ? end_button_->bounds() : Rect{};
    (void)eb;
}

engine::Rect ActionBar::card_rect(const CardView&, int slot) const {
    float base = static_cast<float>(static_cast<int>(bg_w_ / 2.0f - slot_w_ * capacity_ / 2.0f));
    float sx = static_cast<float>(static_cast<int>(base + slot_w_ * slot));
    float sy = static_cast<float>(static_cast<int>(bg_h_ / 2.0f - slot_h_ / 2.0f));
    return Rect{sx, sy, sx + 68, sy + 85};
}

void ActionBar::set_angle(double a) {
    car_.angle = a;
    const float r = 10;
    for (int i = 0; i < 4; ++i) {
        double ang = a + i * (kPi / 2);
        float x = static_cast<float>(r * std::cos(ang)), y = static_cast<float>(r * std::sin(ang));
        float k = 1.5f + (r - std::fabs(y)) / r;
        x *= k;
        car_.clips[i]->set_matrix(Matrix{1, 0, 0, 1, x, y});
        car_.clips[i]->set_child_alpha("dark", (r * 2 - (y + r)) / (r * 4));
    }
}

void ActionBar::carousel_press(bool right) {
    app_.sound().play_sound("i_carousel", false, -1);
    if (right) car_.current = (car_.current + 1) % 4;
    else car_.current = (car_.current + 3) % 4;
    type_ = type_from_carousel(car_.current);
    if (car_.turning_left || car_.turning_right) set_angle(car_.target);
    car_.turning_left = !right;
    car_.turning_right = right;
    car_.target = car_.angle + (right ? -kPi / 2 : kPi / 2);
    if (engine::MovieClip* b = car_.arrows->child(right ? "right" : "left")) b->play();
    car_.counter = 0;
    car_.force = 0;
    if (cb_.card_released) cb_.card_released();
}

void ActionBar::show_type(CardType t) {
    // CardCarousel.setType: turn the carousel to `t`
    int idx = static_cast<int>(t);
    if (idx == car_.current) { type_ = t; return; }
    car_.target = car_.angle - kPi / 2 * (idx - car_.current);
    car_.turning_right = true;
    car_.turning_left = false;
    car_.counter = 0;
    car_.force = 0;
    car_.current = idx;
    type_ = t;
}

void ActionBar::refresh(const sbso::game::Sim& sim) {
    if (!built_) return;
    const auto& db = app_.assets().db();
    bool rebuild = false;
    for (int t = 0; t < 4; ++t) {
        const auto& hand = sim.hand(static_cast<CardType>(t));
        if (hand.size() != cards_[t].size()) { rebuild = true; break; }
        for (size_t i = 0; i < hand.size(); ++i)
            if (hand[i].name != cards_[t][i].name) { rebuild = true; break; }
    }
    if (rebuild) {
        for (int t = 0; t < 4; ++t) {
            std::vector<CardView> views;
            for (const auto& h : sim.hand(static_cast<CardType>(t))) {
                CardView v;
                v.name = h.name;
                v.type = static_cast<CardType>(t);
                const auto* info = db.card(h.name);
                std::string sym = info ? info->symbol : "";
                int img = card_lib_->symbol_id("cards." + sym);
                if (img < 0) img = card_lib_->symbol_id("cards.unavailableCard");
                v.image = img;
                v.cost = info ? std::to_string(info->cost) : "";
                v.power = info ? std::to_string(v.type == CardType::Move ? info->max : info->damage) : "";
                static const char* kFlip[4] = {"MOVE", "ATTACK", "DEFEND", "NICK"};
                v.flip = engine::MovieClip::create(card_lib_, card_lib_->symbol_id(std::string("cards.flip_") + kFlip[t]), &app_);
                v.flip->stop();
                views.push_back(std::move(v));
            }
            cards_[t] = std::move(views);
        }
    }
    int energy = sim.unit(sim.sb_id()).energy;
    int available = 0;
    for (int t = 0; t < 4; ++t) {
        const auto& hand = sim.hand(static_cast<CardType>(t));
        for (size_t i = 0; i < cards_[t].size() && i < hand.size(); ++i) {
            CardView& c = cards_[t][i];
            const auto* info = db.card(c.name);
            bool used = hand[i].used;
            if (used && !c.used) { c.flip->goto_and_play(1); app_.sound().play_sound("battle_card_flip", false, -1); }
            if (!used && c.used) c.flip->goto_and_stop(1);
            c.used = used;
            c.dim = !used && !sim.card_available(c.name);
            (void)energy;
            if (!used && !c.dim) ++available;
            (void)info;
        }
    }
    glow_ = sim.turn() == sbso::game::Turn::Player && available == 0;
}

void ActionBar::set_selected(const std::string& card) {
    selected_ = card;
    for (auto& type : cards_)
        for (auto& c : type) c.loc = (c.name == card && !card.empty()) ? 3 : (&c == hover_ ? 2 : 1);
}

void ActionBar::set_enabled(bool e) { enabled_ = e; }

ActionBar::CardView* ActionBar::hit_card(Point local) {
    auto& vec = cards_[static_cast<int>(type_)];
    for (size_t i = 0; i < vec.size(); ++i) {
        CardView& c = vec[i];
        if (c.used || c.dim) continue;
        Rect r = card_rect(c, static_cast<int>(i));
        if (c.loc == 3) { r.y0 -= 10; r.y1 -= 10; }
        if (inside(r, local)) return &c;
    }
    return nullptr;
}

void ActionBar::tick() {
    if (!built_) return;
    y_ -= (y_ - target_y_) * 0.45f;
    if (std::fabs(y_ - target_y_) < 0.01f) y_ = target_y_;
    // carousel physics (CardCarousel.turnLeft/turnRight)
    if (car_.turning_left || car_.turning_right) {
        if (car_.counter >= car_.num_updates) {
            set_angle(car_.target);
            if (car_.turning_left && car_.angle >= kPi * 2) car_.angle -= kPi * 2;
            if (car_.turning_right && car_.angle < 0) car_.angle += kPi * 2;
            car_.turning_left = car_.turning_right = false;
        } else {
            car_.force += (car_.target - car_.angle) * 0.4;
            car_.force *= 0.7;
            set_angle(car_.angle + car_.force);
            ++car_.counter;
        }
    }
    for (auto* b : {car_.arrows->child("left"), car_.arrows->child("right")}) {
        if (!b) continue;
        b->tick();
        if (b->current_frame() == 0) b->stop();  // addFrameScript(0, stop)
    }
    for (auto& type : cards_)
        for (auto& c : type) {
            if (c.flip && c.used) { c.flip->tick(); if (c.flip->current_frame() == c.flip->total_frames() - 1) c.flip->stop(); }
        }
    if (glow_clip_) glow_clip_->tick();
}

void ActionBar::draw(std::vector<engine::DrawCmd>& out) {
    if (!built_ || y_ >= off_y_ - 0.5f) return;
    Matrix origin{1, 0, 0, 1, x_, y_};
    engine::ColorTransform bgcx;
    bgcx.mult[3] = 0.8f;
    bg_->collect(out, origin, bgcx);
    float base = static_cast<float>(static_cast<int>(bg_w_ / 2.0f - slot_w_ * capacity_ / 2.0f));
    float sy = static_cast<float>(static_cast<int>(bg_h_ / 2.0f - slot_h_ / 2.0f));
    engine::ColorTransform slotcx;  // ColorTransform(0,0,0,0.2): black at 20 %
    slotcx.mult[0] = slotcx.mult[1] = slotcx.mult[2] = 0;
    slotcx.mult[3] = 0.2f;
    for (int i = 0; i < capacity_ && i < static_cast<int>(slots_.size()); ++i)
        slots_[i]->collect(out, origin * Matrix{1, 0, 0, 1, static_cast<float>(static_cast<int>(base + slot_w_ * i)), sy}, slotcx);
    // carousel: children sorted by y (back to front)
    Matrix cm = origin * Matrix{1, 0, 0, 1, car_.x, car_.y};
    std::vector<int> order{0, 1, 2, 3};
    std::sort(order.begin(), order.end(), [&](int a, int b) { return car_.clips[a]->matrix().ty < car_.clips[b]->matrix().ty; });
    for (int i : order) car_.clips[i]->collect(out, cm * car_.clips[i]->matrix(), engine::ColorTransform{});
    car_.arrows->collect(out, cm * Matrix{1, 0, 0, 1, 0, 17}, engine::ColorTransform{});
    // cards of the current type
    auto& vec = cards_[static_cast<int>(type_)];
    static const float kWhite[4] = {1, 1, 1, 1};
    for (size_t i = 0; i < vec.size(); ++i) {
        CardView& c = vec[i];
        Rect r = card_rect(c, static_cast<int>(i));
        float dy = c.loc == 3 ? -10.0f : (c.loc == 2 ? -5.0f : 0.0f);
        Matrix cmx = origin * Matrix{1, 0, 0, 1, r.x0, r.y0};
        engine::ColorTransform cx;
        if (c.dim) cx.mult[0] = cx.mult[1] = cx.mult[2] = 0.5f;  // ColorMatrixFilter(0.5 diag)
        if (!c.used) {
            engine::DrawCmd d;
            if (engine::image_cmd(card_lib_, c.image, cmx * Matrix{1, 0, 0, 1, 0, dy}, &d, cx)) out.push_back(d);
        }
        if (c.flip) c.flip->collect(out, cmx, cx);
        if (!c.used) {
            out.push_back(engine::text_cmd("unibody8black", 8, kWhite, c.cost, cmx, 18, 72 + dy));
            out.push_back(engine::text_cmd("unibody8black", 8, kWhite, c.power, cmx, 43, 72 + dy));
        }
    }
    // end-turn button
    float bx = bg_w_ / 2 + slot_w_ * capacity_ / 2 + 10;
    Rect eb = end_button_->bounds();
    float by = bg_h_ / 2 - eb.h() / 2;
    if (glow_) glow_clip_->collect(out, origin * Matrix{1, 0, 0, 1, bx - 44, by - 47}, engine::ColorTransform{});
    end_button_->collect(out, origin * Matrix{1, 0, 0, 1, bx, by}, engine::ColorTransform{});
}

void ActionBar::pointer_move(Point p) {
    pointer_ = p;
    if (!built_ || !enabled_) return;
    Point local{p.x - x_, p.y - y_};
    CardView* h = hit_card(local);
    if (h != hover_) {
        if (hover_ && hover_->name != selected_) hover_->loc = 1;
        hover_ = h;
        if (h) {
            app_.sound().play_sound("battle_card_rollover", false, -1);
            if (h->name != selected_) h->loc = 2;
            if (selected_.empty() && cb_.card_selected) cb_.card_selected(h->name, false);
        } else if (selected_.empty() && cb_.card_released) {
            cb_.card_released();
        }
    }
    // end button hover
    std::vector<engine::ButtonHit> hits;
    float bx = bg_w_ / 2 + slot_w_ * capacity_ / 2 + 10;
    Rect eb = end_button_->bounds();
    Matrix m = Matrix{1, 0, 0, 1, x_ + bx, y_ + bg_h_ / 2 - eb.h() / 2};
    end_button_->collect_buttons(hits, m);
    bool over = false;
    for (auto& h2 : hits) {
        if (h2.contains(p)) over = true;
    }
    if (over != end_hover_) {
        end_hover_ = over;
        for (auto& h2 : hits) h2.owner->set_button_state(h2.depth, over ? (end_down_ ? engine::MovieClip::ButtonState::Down : engine::MovieClip::ButtonState::Over) : engine::MovieClip::ButtonState::Up);
    }
}

bool ActionBar::pointer_down(Point p) {
    if (!built_ || y_ >= off_y_ - 0.5f) return false;
    Point local{p.x - x_, p.y - y_};
    if (local.x < 0 || local.y < 0 || local.x >= bg_w_ || local.y >= bg_h_) return false;
    if (!enabled_) return true;
    // arrows (mouse down on the sprites)
    Matrix cm = Matrix{1, 0, 0, 1, car_.x, car_.y + 17};
    for (bool right : {false, true}) {
        engine::MovieClip* b = car_.arrows->child(right ? "right" : "left");
        if (!b) continue;
        Rect r = b->bounds();
        float ox = 0, oy = 0;
        car_.arrows->get_child_xy(right ? "right" : "left", &ox, &oy);
        Rect rr{cm.tx + ox + r.x0, cm.ty + oy + r.y0, cm.tx + ox + r.x1, cm.ty + oy + r.y1};
        if (inside(rr, local)) { carousel_press(right); return true; }
    }
    end_down_ = end_hover_;
    if (end_hover_) {
        std::vector<engine::ButtonHit> hits;
        Rect eb = end_button_->bounds();
        end_button_->collect_buttons(hits, Matrix{1, 0, 0, 1, x_ + bg_w_ / 2 + slot_w_ * capacity_ / 2 + 10, y_ + bg_h_ / 2 - eb.h() / 2});
        for (auto& h : hits) h.owner->set_button_state(h.depth, engine::MovieClip::ButtonState::Down);
    }
    return true;
}

bool ActionBar::pointer_up(Point p) {
    if (!built_ || y_ >= off_y_ - 0.5f) return false;
    Point local{p.x - x_, p.y - y_};
    bool in_bar = !(local.x < 0 || local.y < 0 || local.x >= bg_w_ || local.y >= bg_h_);
    if (!in_bar) { end_down_ = false; return false; }
    if (!enabled_) return true;
    if (end_down_ && end_hover_) {
        end_down_ = false;
        app_.sound().play_sound("endturn_button", false, -1);
        if (cb_.end_turn) cb_.end_turn();
        return true;
    }
    end_down_ = false;
    if (CardView* c = hit_card(local)) {
        app_.sound().play_sound("battle_card_select", false, -1);
        if (cb_.card_selected) cb_.card_selected(c->name, true);
    }
    return true;
}

}  // namespace sbso::app
