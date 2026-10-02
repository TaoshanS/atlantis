#include "card_carousel.h"

#include <algorithm>
#include <cmath>

namespace sbso::app {
using engine::Matrix;
using engine::Point;
using engine::Rect;

namespace {
constexpr float kPi = 3.14159265f;
const char* kOrder[4] = {"NICK", "MOVE", "ATTACK", "DEFENSE"};  // carousel clip i
bool inside(const Rect& r, Point p) { return p.x >= r.x0 && p.x < r.x1 && p.y >= r.y0 && p.y < r.y1; }
}  // namespace

CardCarousel::CardCarousel(App& app) : app_(app) {
    lib_ = app.assets().library("cardHolder");
    if (!lib_) return;
    for (int i = 0; i < 4; ++i) clips_[i] = engine::MovieClip::create(lib_, lib_->symbol_id(std::string("CardHolder.Carousel.") + kOrder[i]), &app);
    arrows_ = engine::MovieClip::create(lib_, lib_->symbol_id("CardHolder.Carousel.ArrowButtons"), &app);
    arrows_->stop();
    for (const char* n : {"left", "right"})
        if (engine::MovieClip* b = arrows_->child(n)) b->stop();
    set_angle(0);
    built_ = true;
}

void CardCarousel::set_angle(double a) {
    angle_ = a;
    const float r = 10;
    for (int i = 0; i < 4; ++i) {
        double ang = a + i * (kPi / 2);
        float x = static_cast<float>(r * std::cos(ang)), y = static_cast<float>(r * std::sin(ang));
        float k = 1.5f + (r - std::fabs(y)) / r;
        x *= k;
        clips_[i]->set_matrix(Matrix{1, 0, 0, 1, x, y});
        clips_[i]->set_child_alpha("dark", (r * 2 - (y + r)) / (r * 4));
    }
}

void CardCarousel::press(bool right) {
    app_.sound().play_sound("i_carousel", false, -1);
    current_ = right ? (current_ + 1) % 4 : (current_ + 3) % 4;
    if (turning_left_ || turning_right_) set_angle(target_);
    turning_left_ = !right;
    turning_right_ = right;
    target_ = angle_ + (right ? -kPi / 2 : kPi / 2);
    if (engine::MovieClip* b = arrows_->child(right ? "right" : "left")) b->play();
    counter_ = 0;
    force_ = 0;
    if (on_type) on_type(current_);
}

void CardCarousel::set_type(int idx) {
    if (idx == current_) return;
    target_ = angle_ - kPi / 2 * (idx - current_);
    turning_right_ = true;
    turning_left_ = false;
    counter_ = 0;
    force_ = 0;
    current_ = idx;
}

void CardCarousel::tick() {
    if (!built_) return;
    if (turning_left_ || turning_right_) {
        if (counter_ >= num_updates_) {
            set_angle(target_);
            if (turning_left_ && angle_ >= kPi * 2) angle_ -= kPi * 2;
            if (turning_right_ && angle_ < 0) angle_ += kPi * 2;
            turning_left_ = turning_right_ = false;
        } else {
            force_ += (target_ - angle_) * 0.4;
            force_ *= 0.7;
            set_angle(angle_ + force_);
            ++counter_;
        }
    }
    for (auto* b : {arrows_->child("left"), arrows_->child("right")}) {
        if (!b) continue;
        b->tick();
        if (b->current_frame() == 0) b->stop();
    }
}

void CardCarousel::draw(std::vector<engine::DrawCmd>& out, const Matrix& origin, float scale) const {
    if (!built_ || scale <= 0) return;
    Matrix cm = origin * Matrix{scale, 0, 0, scale, 0, 0};
    std::vector<int> order{0, 1, 2, 3};
    std::sort(order.begin(), order.end(), [&](int a, int b) { return clips_[a]->matrix().ty < clips_[b]->matrix().ty; });
    for (int i : order) clips_[i]->collect(out, cm * clips_[i]->matrix(), engine::ColorTransform{});
    arrows_->collect(out, cm * Matrix{1, 0, 0, 1, 0, 17}, engine::ColorTransform{});
}

bool CardCarousel::pointer_down(Point local) {
    if (!built_) return false;
    for (bool right : {false, true}) {
        engine::MovieClip* b = arrows_->child(right ? "right" : "left");
        if (!b) continue;
        Rect r = b->bounds();
        float ox = 0, oy = 0;
        arrows_->get_child_xy(right ? "right" : "left", &ox, &oy);
        Rect rr{ox + r.x0, 17 + oy + r.y0, ox + r.x1, 17 + oy + r.y1};
        if (inside(rr, local)) { press(right); return true; }
    }
    return false;
}

}  // namespace sbso::app
