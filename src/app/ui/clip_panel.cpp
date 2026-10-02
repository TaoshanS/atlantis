#include "clip_panel.h"

namespace sbso::app {
using engine::MovieClip;

ClipPanel::ClipPanel(std::unique_ptr<MovieClip> clip, engine::Matrix place, std::vector<std::string> names)
    : clip_(std::move(clip)), place_(place), names_(std::move(names)) {}

std::string ClipPanel::name_at(engine::Point p) {
    if (!clip_) return {};
    std::vector<engine::ButtonHit> hits;
    clip_->collect_buttons(hits, place_);
    std::string found;
    for (float m : {0.0f, engine::ButtonHit::slop}) {  // a direct hit first; then, on touch screens, the nearest margin
        for (const auto& h : hits) {
            if (!h.within(p, m)) continue;
            for (const auto& n : names_)
                if (n == "*" || h.name == n || h.owner->in_named(n)) { found = n == "*" ? (h.name.empty() ? std::string("*") : h.name) : n; break; }  // later hits are painted on top
        }
        if (!found.empty() || m >= engine::ButtonHit::slop) break;
    }
    return found;
}

void ClipPanel::update_hover(App& app) {
    if (!clip_) return;
    std::vector<engine::ButtonHit> hits;
    clip_->collect_buttons(hits, place_);
    const engine::ButtonHit* top = nullptr;
    std::string hovered;
    for (float m : {0.0f, engine::ButtonHit::slop}) {
        for (const auto& h : hits) {
            if (!h.within(pointer_, m)) continue;
            for (const auto& n : names_)
                if (n == "*" || h.name == n || h.owner->in_named(n)) { hovered = n == "*" ? (h.name.empty() ? std::string("*") : h.name) : n; top = &h; break; }
        }
        if (top || m >= engine::ButtonHit::slop) break;
    }
    // final state per button, applied once (entering a state restarts its sprites, so no Up->Over flip-flop on every pointer move)
    for (auto& h : hits) {
        bool is_top = top && h.owner == top->owner && h.depth == top->depth;
        h.owner->set_button_state(h.depth, is_top ? (down_ ? MovieClip::ButtonState::Down : MovieClip::ButtonState::Over) : MovieClip::ButtonState::Up);
    }
    if (hovered != hover_) {
        if (!hovered.empty()) app.sound().play_sound("button_rollover", false, -1);
        hover_ = hovered;
    }
}

void ClipPanel::pointer_move(App& app, engine::Point p) { pointer_ = p; update_hover(app); }

void ClipPanel::pointer_down(App& app, engine::Point p) {
    pointer_ = p;
    update_hover(app);
    down_ = true;
    down_name_ = hover_;
    update_hover(app);
}

std::string ClipPanel::pointer_up(App& app, engine::Point p) {
    pointer_ = p;
    bool was_down = down_;
    down_ = false;
    update_hover(app);
    std::string r;
    if (was_down && !down_name_.empty() && down_name_ == hover_) r = down_name_;
    down_name_.clear();
    return r;
}

}  // namespace sbso::app
