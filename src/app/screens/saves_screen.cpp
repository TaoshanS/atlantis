#include "saves_screen.h"

#include <cstdio>
#include <ctime>

#include "app/ui/menu_screens.h"
#include "app/ui/skin.h"
#include "engine/draw_util.h"

namespace sbso::app {
using engine::Matrix;
using engine::Point;
using engine::Rect;

namespace {
enum Action { kSave = 1, kLoad, kDelete, kClose };
// Desktop metrics; phones get the whole stage, taller rows and bigger buttons and text (finger-sized targets).
struct Metrics { Rect panel; float row_h, btn_h, btn_w, label, detail, btn_text, gap; };
const Metrics kDesk{{40, 24, 600, 456}, 26, 20, 46, 13, 10, 11, 6};
const Metrics kPhone{{2, 0, 638, 480}, 35, 30, 78, 17, 13, 15, 8};
const Metrics& M() { return kMobile ? kPhone : kDesk; }
const float kWhite[4] = {1, 1, 1, 1}, kDim[4] = {1, 1, 1, 0.55f}, kTitle[4] = {1.0f, 0.86f, 0.3f, 1};

bool inside(const Rect& r, Point p) {
    const float s = engine::ButtonHit::slop * 0.5f;  // touch margin (half: the rows are close together)
    return p.x >= r.x0 - s && p.x < r.x1 + s && p.y >= r.y0 - s && p.y < r.y1 + s;
}

std::string format_time(std::int64_t t) {
    if (t <= 0) return "";
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}
}  // namespace

SavesScreen::SavesScreen(App& app, std::function<void()> on_loaded) : app_(app), skin_(app), on_loaded_(std::move(on_loaded)) { rebuild(); }

void SavesScreen::rebuild() {
    slots_ = app_.list_slots();
    buttons_.clear();
    bool profile = !app_.profile_id().empty();
    const Metrics& m = M();
    Rect pl = Skin::plate_of(m.panel);
    float rowY = pl.y0 + 56;
    for (int i = 0; i <= save::kManualSlots; ++i) {
        float y = rowY + i * m.row_h;
        bool exists = i < static_cast<int>(slots_.size()) && slots_[i].exists && !slots_[i].corrupt;
        float x1 = pl.x1 - 14, h = m.btn_h, by = y + (m.row_h - 3 - h) / 2, w = m.btn_w, g = m.gap;
        if (i >= 1) buttons_.push_back({Rect{x1 - w, by, x1, by + h}, "DEL", kDelete, i, exists});
        buttons_.push_back({Rect{x1 - 2 * w - g, by, x1 - w - g, by + h}, "LOAD", kLoad, i, exists});
        if (i >= 1) buttons_.push_back({Rect{x1 - 3 * w - 2 * g, by, x1 - 2 * w - 2 * g, by + h}, "SAVE", kSave, i, profile});
    }
    float cx = (m.panel.x0 + m.panel.x1) / 2, cw = kMobile ? 60.0f : 30.0f, ch = kMobile ? 34.0f : 24.0f;
    float cy = kMobile ? m.panel.y1 - ch - 2 : m.panel.y1 - 14;
    buttons_.push_back({Rect{cx - cw, cy, cx + cw, cy + ch}, "CLOSE", kClose, 0, true});
}

void SavesScreen::pointer_up(App& app, Point p) {
    for (const Button& b : buttons_) {
        if (!b.enabled || !inside(b.r, p)) continue;
        app.sound().play_sound("button_click", false, -1);
        switch (b.action) {
            case kSave:
                message_ = app.save_to_slot(b.slot) ? app.tr("saves.saved", "Game saved") : app.tr("saves.failed", "Could not save");
                break;
            case kLoad:
                if (app.load_from_slot(b.slot)) {
                    app.close_overlay();
                    if (on_loaded_) on_loaded_();
                    return;
                }
                message_ = app.tr("saves.failed", "Could not load");
                break;
            case kDelete:
                app.delete_slot(b.slot);
                message_.clear();
                break;
            case kClose:
                app.close_overlay();
                return;
        }
        rebuild();
        return;
    }
}

void SavesScreen::key_down(App& app, int key) {
    if (key == 27) app.close_overlay();
}

void SavesScreen::draw(App& app, std::vector<engine::DrawCmd>& out) {
    skin_.begin();
    draw_dim(out, 0.7f);
    const Metrics& m = M();
    Rect pl = skin_.panel(out, m.panel);
    skin_.text(out, app.tr("saves.title", "SAVED GAMES"), pl.x0, pl.y0 + 8, 24, 2, pl.w());
    std::string who = app.current_profile_name();
    if (who.empty()) who = app.tr("saves.noprofile", "(create a profile first)");
    skin_.text(out, who, pl.x0, pl.y0 + 36, 13, 2, pl.w(), true);
    float rowY = pl.y0 + 56;
    for (int i = 0; i <= save::kManualSlots; ++i) {
        float y = rowY + i * m.row_h;
        skin_.plate(out, Rect{pl.x0 + 8, y, pl.x1 - 8, y + m.row_h - 3}, 6, i == 0 ? -0.05f : 0.16f);
        bool exists = i < static_cast<int>(slots_.size()) && slots_[i].exists;
        std::string label = i == 0 ? app.tr("saves.auto", "AUTOSAVE") : app.tr("saves.slot", "SLOT") + " " + std::to_string(i);
        skin_.text(out, label, pl.x0 + 18, y + (m.row_h - 3 - m.label) / 2 - 1, m.label);
        std::string detail = !exists ? app.tr("saves.empty", "empty") : format_time(slots_[i].modified_unix);
        if (exists && slots_[i].corrupt) detail = app.tr("saves.corrupt", "damaged");
        else if (exists && slots_[i].has_snapshot) detail += "  " + app.tr("saves.battle", "(battle in progress)");
        skin_.text(out, detail, pl.x0 + (kMobile ? 120 : 104), y + (m.row_h - 3 - m.detail) / 2, m.detail, 0, 0, true);
    }
    for (const Button& b : buttons_) {
        bool hover = b.enabled && inside(b.r, pointer_);
        bool red = b.action == kDelete || b.action == kClose;
        skin_.button(out, b.r, b.label, hover, b.enabled, red, b.action == kClose ? m.btn_text + 2 : m.btn_text);
    }
    if (!message_.empty()) skin_.text(out, message_, pl.x0, pl.y1 - 22, 12, 2, pl.w());
}

}  // namespace sbso::app
