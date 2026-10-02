#include "gauge_screen.h"

#include <cmath>

namespace sbso::app {
using engine::Matrix;

GaugeScreen::GaugeScreen(App& app, int max_damage, std::function<void(double, bool)> done) : max_damage_(max_damage), done_(std::move(done)) {
    const engine::Library* lib = app.assets().library("CounterAttackGaugue");
    if (lib) clip_ = engine::MovieClip::create(lib, lib->symbol_id("CounterAttackGauge"), &app);
    if (!clip_) finished_ = true;
}

double GaugeScreen::power() const {
    engine::MovieClip* g = clip_ ? clip_->child("cGauge") : nullptr;
    int f = g ? g->current_frame() + 1 : 1;
    if (f < 9) return 0;
    return max_damage_ - std::fabs(static_cast<double>(f - 30)) / 20.0 * max_damage_;
}

void GaugeScreen::finish(App& app, double p, bool timeout) {
    if (finished_ && !clip_) { if (done_) { auto cb = std::move(done_); done_ = nullptr; app.close_overlay(); cb(0, true); } return; }
    finished_ = true;
    delay_ = timeout ? 0 : 12;  // let the hit animation of the button play briefly
    app.sound().play_sound(timeout ? "button_click" : "battle_click_attack_card_target", false, -1);
    auto cb = std::move(done_);
    done_ = nullptr;
    pending_p_ = p;
    pending_timeout_ = timeout;
    pending_cb_ = std::move(cb);
}

void GaugeScreen::tick(App& app) {
    if (!clip_) { finish(app, 0, true); return; }
    if (finished_) {
        if (--delay_ <= 0 && pending_cb_) { auto cb = std::move(pending_cb_); pending_cb_ = nullptr; app.close_overlay(); cb(pending_p_, pending_timeout_); }
        return;
    }
    clip_->tick();
    engine::MovieClip* g = clip_->child("cGauge");
    if (g && g->current_frame() >= g->total_frames() - 1) finish(app, 0, true);  // frame 51 script: TIME_UP
}

void GaugeScreen::pointer_down(App& app, engine::Point) {
    if (finished_ || !clip_) return;
    if (auto* b = clip_->child("cButton")) b->goto_label_and_stop("down");  // MOUSE_DOWN: cButton.gotoAndStop(...)
    finish(app, power(), false);
}

void GaugeScreen::key_down(App& app, int key) {
    if (key == 13 || key == 32) pointer_down(app, {0, 0});
}

void GaugeScreen::draw(App&, std::vector<engine::DrawCmd>& out) {
    if (clip_) clip_->collect(out, Matrix{1, 0, 0, 1, 320, 320}, engine::ColorTransform{});
}

}  // namespace sbso::app
