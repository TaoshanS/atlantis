#include "options_screen.h"

#include <cmath>

#include "engine/draw_util.h"

namespace sbso::app {
using engine::Matrix;
using engine::Point;
using engine::Rect;

namespace {

bool inside(const Rect& r, Point p) { return p.x >= r.x0 && p.x < r.x1 && p.y >= r.y0 && p.y < r.y1; }

float cos_interp(float a, float b, float t) {  // Utils.cosInterp
    float k = (1.0f - std::cos(t * 3.14159265f)) / 2.0f;
    return a * (1 - k) + b * k;
}

}  // namespace

// The language / graphics rows sit in a second bamboo panel stacked on top of the original options panel (which has no spare room).
Rect OptionsScreen::extra_panel() { return Rect{53, 173, 583, 329}; }

OptionsScreen::OptionsScreen(App& app) : app_(app), skin_(app) {
    lib_ = app.assets().library("menu_assets");
    if (!lib_) return;
    bg_ = engine::MovieClip::create(lib_, lib_->symbol_id("optionsScreen"), &app);
    checkbox_on_ = engine::MovieClip::create_any(lib_, lib_->symbol_id("fullscreen_on"), &app);
    checkbox_off_ = engine::MovieClip::create_any(lib_, lib_->symbol_id("fullscreen_off"), &app);
    thumbs_[0] = {"cThumb_0", "cBar_0", "cUnderBar_0"};
    thumbs_[1] = {"cThumb_1", "cBar_1", "cUnderBar_1"};
    for (int i = 0; i < 2; ++i) {  // initButtons: bars start at their track, thumbs animate to the stored volume
        float x, y;
        bg_->get_child_xy(thumbs_[i].under, &x, &y);
        bg_->set_child_xy(thumbs_[i].bar, x, y);
        float v = static_cast<float>(i == 0 ? app.settings().fx_vol : app.settings().music_vol);
        init_thumb_anim(i, v * (bounds_right_ - bounds_left_) + bounds_left_);
    }
    language_value_ = app.settings().language;
    graphics_value_ = app.settings().graphics_filter;
    rebuild_choices(app);
}

void OptionsScreen::rebuild_choices(App& app) {
    // Rows live above the original panel (which has no spare room): label on the left, two toggle buttons on the right.
    language_ = {{"English", "en", {}, false}, {"Castellano", "es-ES", {}, false}};
    graphics_ = {{app.tr("options.original", "Original"), "original", {}, false}, {app.tr("options.smooth", "Smooth"), "lanczos", {}, false}};
    // an empty language setting means "follow the system": highlight what is actually active
    std::string active_lang = language_value_.empty() ? app.strings().language() : language_value_;
    if (active_lang.rfind("es", 0) == 0) active_lang = "es-ES";
    Rect pl = Skin::plate_of(extra_panel());
    float x0 = pl.x0 + 150, w = 140, h = 24, gap = 10;
    float y_lang = pl.y0 + 8, y_gfx = pl.y0 + 38;
    for (size_t i = 0; i < language_.size(); ++i) {
        language_[i].rect = Rect{x0 + i * (w + gap), y_lang, x0 + i * (w + gap) + w, y_lang + h};
        language_[i].selected = language_[i].value == active_lang;
    }
    for (size_t i = 0; i < graphics_.size(); ++i) {
        graphics_[i].rect = Rect{x0 + i * (w + gap), y_gfx, x0 + i * (w + gap) + w, y_gfx + h};
        graphics_[i].selected = graphics_[i].value == graphics_value_;
    }
}

void OptionsScreen::set_thumb(int i, float x) {
    x = std::max(bounds_left_, std::min(bounds_right_, x));
    float ox, oy;
    bg_->get_child_xy(thumbs_[i].thumb, &ox, &oy);
    bg_->set_child_xy(thumbs_[i].thumb, x, oy);
    // adjustBarWidth: the bar is as wide as the distance covered by the thumb
    Rect b;
    if (bg_->child_bounds(thumbs_[i].bar, &b)) {
        float bar_x, bar_y;
        bg_->get_child_xy(thumbs_[i].bar, &bar_x, &bar_y);
        float natural = b.w();  // current width at scale 1 is unknown after scaling: measure the track instead
        Rect track;
        if (bg_->child_bounds(thumbs_[i].under, &track)) natural = track.w();
        float want = x - bounds_left_;
        bg_->set_child_scale_x(thumbs_[i].bar, natural > 0 ? std::max(0.0f, want) / natural : 1.0f);
    }
}

void OptionsScreen::init_thumb_anim(int i, float target) {
    target = std::max(bounds_left_, std::min(bounds_right_, target));
    float x, y;
    bg_->get_child_xy(thumbs_[i].thumb, &x, &y);
    if (x == target) { set_thumb(i, x); return; }
    Thumb& t = thumbs_[i];
    float dist = std::fabs(x - target);
    t.prog = 0;
    t.inc = dist > 10 ? 10 / dist : 10;
    t.a = x;
    t.b = target;
    t.animating = true;
}

void OptionsScreen::apply_volume(int i) {
    float x, y;
    bg_->get_child_xy(thumbs_[i].thumb, &x, &y);
    float v = (x - bounds_left_) / (bounds_right_ - bounds_left_);
    if (v < 0.05f) v = 0;
    if (i == 0) { app_.settings().fx_vol = v; app_.sound().set_effects_volume(v); }
    else { app_.settings().music_vol = v; app_.sound().set_all_music_volume(v); }
}

void OptionsScreen::tick(App&) {
    if (!bg_) return;
    for (int i = 0; i < 2; ++i) {
        Thumb& t = thumbs_[i];
        if (!t.animating) continue;
        t.prog += t.inc;
        if (t.prog < 1) {
            set_thumb(i, cos_interp(t.a, t.b, t.prog));
            if (!intro_) apply_volume(i);
        } else {
            intro_ = false;
            set_thumb(i, t.b);
            t.animating = false;
        }
    }
}

void OptionsScreen::checkbox_geometry(Matrix* m, Rect* hit) const {
    float x, y;
    bg_->get_child_xy("cCheckBox", &x, &y);
    *m = Matrix{1, 0, 0, 1, origin_.x + x, origin_.y + y};
    *hit = Rect{m->tx, m->ty, m->tx + 30, m->ty + 24};
}

void OptionsScreen::draw(App& app, std::vector<engine::DrawCmd>& out) {
    if (kMobile && bg_) bg_->enlarge_named("cCloseButton", kMobileCloseScale);
    if (!bg_) return;
    float left = -(app.layout().logical_w - sbso::kDesignW) / 2.0f;
    shapes_.clear();
    shapes_.reserve(64);
    // dim everything behind (the extended scene is dimmed too)
    shapes_.push_back(engine::round_rect(left - 20, -20, app.layout().logical_w + 40, sbso::kDesignH + 40, 0, 0, 0, 0, 0.65f));
    {
        engine::DrawCmd d;
        d.kind = engine::DrawCmd::Kind::Solid;
        d.solid = &shapes_.back();
        out.push_back(d);
    }
    skin_.begin();
    // second panel first: the original panel is drawn over its lower bamboo bar so the two read as stacked planks
    Rect pl = skin_.panel(out, extra_panel());
    auto draw_row = [&](const std::string& label, std::vector<Choice>& row, float y) {
        skin_.text(out, label, pl.x0 + 14, y + 4, 16);
        for (const Choice& c : row) {
            bool hover = hover_ == c.value + "@" + label;
            skin_.button(out, c.rect, c.label, hover, c.selected, false, 14);
        }
    };
    draw_row(app.tr("options.language", "LANGUAGE:"), language_, pl.y0 + 8);
    draw_row(app.tr("options.graphics", "GRAPHICS:"), graphics_, pl.y0 + 38);
    bg_->collect(out, Matrix{1, 0, 0, 1, origin_.x, origin_.y}, engine::ColorTransform{});
    Matrix cm;
    Rect hit;
    checkbox_geometry(&cm, &hit);
    (app.settings().fullscreen ? checkbox_on_ : checkbox_off_)->collect(out, cm, engine::ColorTransform{});
}

void OptionsScreen::pointer_move(App&, Point p) {
    pointer_ = p;
    if (!bg_) return;
    if (dragging_ >= 0) {
        set_thumb(dragging_, bg_x(p) - drag_offset_);
        apply_volume(dragging_);
    }
    hover_.clear();
    for (const Choice& c : language_) if (inside(c.rect, p)) hover_ = c.value + "@" + app_.tr("options.language", "LANGUAGE:");
    for (const Choice& c : graphics_) if (inside(c.rect, p)) hover_ = c.value + "@" + app_.tr("options.graphics", "GRAPHICS:");
}

void OptionsScreen::pointer_down(App& app, Point p) {
    pointer_ = p;
    pointer_down_ = true;
    if (!bg_) return;
    for (int i = 0; i < 2; ++i) {
        Rect r;
        if (bg_->child_bounds(thumbs_[i].thumb, &r) && inside(Rect{origin_.x + r.x0, origin_.y + r.y0, origin_.x + r.x1, origin_.y + r.y1}, p)) {
            float x, y;
            bg_->get_child_xy(thumbs_[i].thumb, &x, &y);
            thumbs_[i].animating = false;
            dragging_ = i;
            drag_offset_ = bg_x(p) - x;  // initDrag
            return;
        }
    }
    (void)app;
}

void OptionsScreen::pointer_up(App& app, Point p) {
    pointer_ = p;
    pointer_down_ = false;
    if (!bg_) return;
    if (dragging_ >= 0) { dragging_ = -1; app.save_settings(); return; }
    auto in_child = [&](const std::string& n) {
        Rect r;
        return bg_->child_bounds(n, &r) && inside(Rect{origin_.x + r.x0, origin_.y + r.y0, origin_.x + r.x1, origin_.y + r.y1}, p);
    };
    // buttons (close)
    std::vector<engine::ButtonHit> hits;
    bg_->collect_buttons(hits, Matrix{1, 0, 0, 1, origin_.x, origin_.y});
    for (auto& h : hits)
        if (h.contains(p) && h.owner->in_named("cCloseButton")) { close(app); return; }
    // fullscreen checkbox
    Matrix cm;
    Rect hit;
    checkbox_geometry(&cm, &hit);
    if (inside(hit, p)) {
        app.set_fullscreen(!app.settings().fullscreen);
        app.sound().play_sound("button_click", false, -1);
        app.save_settings();
        return;
    }
    for (int i = 0; i < 2; ++i) {
        if (in_child(thumbs_[i].bar) || in_child(thumbs_[i].under)) { init_thumb_anim(i, bg_x(p)); app.save_settings(); return; }
    }
    struct Inv { const char* name; int thumb; bool right; };
    for (const Inv& v : {Inv{"cInvisible_MuteSFX", 0, false}, Inv{"cInvisible_MuteMUSIC", 1, false}, Inv{"cInvisible_FullSFX", 0, true}, Inv{"cInvisible_FullMUSIC", 1, true}})
        if (in_child(v.name)) { init_thumb_anim(v.thumb, v.right ? bounds_right_ : bounds_left_); app.save_settings(); return; }
    for (const Choice& c : language_)
        if (inside(c.rect, p)) {
            app.sound().play_sound("button_click", false, -1);
            language_value_ = c.value;
            app.set_language(c.value);
            app.save_settings();
            rebuild_choices(app);
            return;
        }
    for (const Choice& c : graphics_)
        if (inside(c.rect, p)) {
            app.sound().play_sound("button_click", false, -1);
            graphics_value_ = c.value;
            app.settings().graphics_filter = c.value;
            app.save_settings();
            rebuild_choices(app);
            return;
        }
}

void OptionsScreen::key_down(App& app, int key) {
    if (key == 27) close(app);  // Escape
}

void OptionsScreen::close(App& app) {
    app.sound().play_sound("button_click", false, -1);
    app.save_settings();
    app.close_overlay();
}

}  // namespace sbso::app
