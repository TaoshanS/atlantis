#include "chest_screen.h"

#include <algorithm>
#include <cmath>

#include "app/flow.h"
#include "app/screens/options_screen.h"
#include "app/ui/help_screen.h"
#include "app/ui/menu_screens.h"
#include "engine/draw_util.h"

namespace sbso::app {
using engine::Matrix;
using engine::MovieClip;
using engine::Point;
using engine::Rect;

namespace {

constexpr float kPi = 3.14159265f;
const char* kTypeNames[4] = {"MOVE", "ATTACK", "DEFEND", "NICK"};
const Point kBds{-88, -5};      // Chest_BattleDeckScreen position inside the chest
constexpr int kCardW = 68, kCardH = 85;
constexpr int kVisibleCols = 5, kVisibleRows = 4;
constexpr float kScrollMin = -136, kScrollMax = 53;
const char* kTabLabels[4] = {"cards", "belts", "coins", "flags"};

bool inside(const Rect& r, Point p) { return p.x >= r.x0 && p.x < r.x1 && p.y >= r.y0 && p.y < r.y1; }

// help_warning_gr: "your deck needs at least one MOVE and one ATTACK card".
class WarningScreen : public Screen {
public:
    WarningScreen(App& app, std::function<void()> done) : done_(std::move(done)) {
        if (const engine::Library* lib = app.assets().library("chest"))
            panel_ = ClipPanel(MovieClip::create(lib, lib->symbol_id("help_warning_gr"), &app), Matrix{}, {"cButt"});
    }
    void tick(App&) override { panel_.tick(); }
    void draw(App&, std::vector<engine::DrawCmd>& out) override { draw_dim(out, 0.65f); panel_.draw(out); }
    void pointer_move(App& app, Point p) override { panel_.pointer_move(app, p); }
    void pointer_down(App& app, Point p) override { panel_.pointer_down(app, p); }
    void pointer_up(App& app, Point p) override {
        if (panel_.pointer_up(app, p).empty()) return;
        app.sound().play_sound("button_click", false, -1);
        app.close_overlay();
        if (done_) done_();
    }

private:
    ClipPanel panel_;
    std::function<void()> done_;
};

}  // namespace

struct ChestScreen::DeckCard {
    std::string name, symbol, cost, power;
    int type = 0, image = -1, row = 0, col = 0;
    int in_deck = -1;
    bool is_new = false;
    float x = 0, y = 0;
    enum class St { Idle, MoveIn, Move, Shake } st = St::Idle;
    Point dest;
    float xf = 0, yf = 0;
    int spring = 0, dir = 0, loc = 1;
    bool hover = false;
    std::unique_ptr<MovieClip> flip, cross, sheen;
    bool displayed = false;
    int index(int cols) const { return col + row * cols; }
};

ChestScreen::ChestScreen(App& app) : app_(app) {
    lib_ = app.assets().library("chest");
    card_lib_ = app.assets().library("cardAssets");
    holder_lib_ = app.assets().library("cardHolder");
    gui_lib_ = app.assets().library("GUI");
    place_ = Matrix{1, 0, 0, 1, sbso::kDesignW / 2.0f, sbso::kDesignH / 2.0f};
    save::Progress& pr = app.progress();
    current_belt_ = belt_shown_ = pr.current_belt_index;
    if (lib_) {
        panel_ = ClipPanel(MovieClip::create(lib_, lib_->symbol_id("chest_screen"), &app), place_,
                           {"button_cards", "button_belts", "button_coins", "button_flags", "button_done", "cHelpButton", "button_scrollup", "button_scrolldn", "button_scrollbar",
                            "beltbtn_0", "beltbtn_1", "beltbtn_2", "beltbtn_3", "beltbtn_4", "beltbtn_5", "beltbtn_6", "beltbtn_7", "beltbtn_8", "beltbtn_9",
                            "cShortcut_0", "cShortcut_1", "cShortcut_2", "cShortcut_3"});
    }
    if (const engine::Library* ml = app.assets().library("menu_assets")) {
        menu_buttons_ = ClipPanel(MovieClip::create(ml, ml->symbol_id("inGameMenuButtons"), &app), Matrix{}, {"cMenuButt"});
        for (const char* n : {"cHelp", "cEyeOpen", "cEyeClosed"}) menu_buttons_.clip()->set_child_visible(n, false);
        Rect b = menu_buttons_.clip()->bounds();
        menu_buttons_.set_place(Matrix{1, 0, 0, 1, sbso::kDesignW - 3 - b.x1, 3 - b.y0});
    }
    set_tab(Tab::Cards);
}

ChestScreen::~ChestScreen() {
    // TreasureChest.recycle: the card types that were looked at are no longer "new"
    for (int t = 0; t < 4; ++t)
        if (viewed_[t]) app_.progress().new_cards[t].clear();
}

// ------------------------------------------------------------------------------------------------ tabs

void ChestScreen::set_tab(Tab t) {
    if (timer_ > 0 || !all_cards_[0].empty() || tab_ != t) exit_tab();
    tab_ = t;
    timer_ = 0;
    buttons_live_ = false;
    if (panel_.valid()) panel_.clip()->goto_label_and_play(kTabLabels[static_cast<int>(t)]);
    enter_tab();
}

void ChestScreen::enter_tab() {
    enabled_ = tab_ != Tab::Cards;  // cards_enter puts the disabler up until the cards have flown in
    if (tab_ == Tab::Cards) {
        intro_done_ = false;
        row_offset_ = 0;
        card_type_ = -1;
        dragging_bar_ = false;
        create_cards();
        carousel_ = std::make_unique<CardCarousel>(app_);
        carousel_->on_type = [this](int t) { change_card_type(t, true); };
        carousel_timer_ = 0;
        if (lib_) radio_ = MovieClip::create(lib_, lib_->symbol_id("BDeckScreen_RadioSet"), &app_);
        if (radio_) {
            const char* names[4] = {"cMove", "cAttack", "cDefend", "cNik"};
            for (int i = 0; i < 4; ++i)
                if (MovieClip* c = radio_->child(names[i])) c->goto_and_stop(i == 0 ? 2 : 1);
        }
        set_card_type(0);
    } else if (tab_ == Tab::Coins) {
        build_coin_stacks();
    }
}

void ChestScreen::exit_tab() {
    if (!all_cards_[0].empty() && (tab_ == Tab::Cards)) {
        has_enough_cards_ = belt_has_min_requirements();
        display_.clear();
        for (auto& t : all_cards_) t.clear();
        carousel_.reset();
        radio_.reset();
        hovered_ = nullptr;
        range_clips_.clear();
        intro_done_ = true;
    }
    belt_buttons_.clear();  // owned by clips of the timeline that is about to change
    belt_shadows_.clear();
    stacks_.clear();
}

void ChestScreen::tab_update() {
    MovieClip* c = panel_.clip();
    switch (tab_) {
        case Tab::Cards:
            if (timer_ == 1) {
                for (const char* n : {"button_scrollup", "button_scrolldn", "button_scrollbar"}) c->set_child_visible(n, false);
            } else if (timer_ == 11) {
                app_.sound().play_sound("chest_icon_cards_rollover", false, -1);
            } else if (timer_ == 17) {
                if (MovieClip* icon = c->child("beltIcon"))
                    if (MovieClip* eq = icon->child("cEquippedBelt")) {
                        const auto& belts = app_.config().belts;
                        int bi = app_.progress().current_belt_index;
                        if (bi >= 0 && bi < static_cast<int>(belts.size())) eq->goto_label_and_stop(belts[bi].name);
                    }
            } else if (timer_ == 28) {
                if (!app_.progress().seen_card_help) { show_help(); app_.progress().seen_card_help = true; }
            } else if (timer_ == 30) {
                buttons_live_ = true;
                update_scroll_bar();
            }
            break;
        case Tab::Belts:
            if (timer_ == 1) build_belt_tab();
            else if (timer_ == 11) app_.sound().play_sound("chest_icon_belt_rollover", false, -1);
            else if (timer_ == 30) {
                buttons_live_ = true;
                change_belt_display(current_belt_, false);
                if (!app_.progress().seen_belt_help) { show_help(); app_.progress().seen_belt_help = true; }
            }
            break;
        case Tab::Coins:
            if (timer_ == 1) {
                for (int i = 0; i < 4; ++i)
                    if (app_.progress().minigame_plays[i] < 1) c->set_child_visible("cShortcut_" + std::to_string(i), false);
            } else if (timer_ == 11) app_.sound().play_sound("chest_icon_coin_rollover", false, -1);
            else if (timer_ == 30) {
                buttons_live_ = true;
                if (!app_.progress().seen_coin_help) { show_help(); app_.progress().seen_coin_help = true; }
            }
            break;
        case Tab::Flags:
            if (timer_ == 2) update_map_flags();
            else if (timer_ == 11) app_.sound().play_sound("chest_move_carousel_and_change_types", false, -1);
            else if (timer_ == 30) {
                buttons_live_ = true;
                if (!app_.progress().seen_flag_help) { show_help(); app_.progress().seen_flag_help = true; }
            }
            break;
    }
}

void ChestScreen::show_help() {
    static const char* names[4] = {"help_cards_contents", "help_belts_contents", "help_coins_contents", "help_flags_contents"};
    app_.open_overlay(std::make_unique<HelpScreen>(app_, names[static_cast<int>(tab_)], false, nullptr));
}

void ChestScreen::show_warning() {
    enabled_ = false;
    app_.open_overlay(std::make_unique<WarningScreen>(app_, [this]() { enabled_ = true; }));
}

bool ChestScreen::belt_has_min_requirements() {
    if (all_cards_[0].empty()) return has_enough_cards_;
    save::Progress& pr = app_.progress();
    std::array<std::vector<std::string>, 4> belted;
    for (int t = 0; t < 4; ++t)
        for (const auto& c : all_cards_[t])
            if (c->in_deck != -1) belted[t].push_back(c->name);
    for (int t = 0; t < 4; ++t) pr.belted[t] = belted[t];
    return !belted[0].empty() && !belted[1].empty();
}

void ChestScreen::done_clicked() {
    if (tab_ == Tab::Cards) has_enough_cards_ = belt_has_min_requirements();
    if (!has_enough_cards_) show_warning();
    else flow::level_select(app_);
}

void ChestScreen::exit_clicked() {
    if (tab_ == Tab::Cards) has_enough_cards_ = belt_has_min_requirements();
    if (!has_enough_cards_) show_warning();
    else { app_.autosave(); flow::title(app_, true); }
}

void ChestScreen::open_menu() {
    app_.sound().play_sound("mapscreen_click_menu_button", false, -1);
    enabled_ = false;
    app_.open_overlay(std::make_unique<InGameMenuScreen>(app_, [this](const std::string& w) {
        enabled_ = true;
        if (w == "exit") exit_clicked();
        else if (w == "options") app_.open_overlay(std::make_unique<OptionsScreen>(app_));
        else if (w == "saves") flow::saved_games(app_);
        else if (w == "instructions") app_.open_overlay(std::make_unique<InstructionsScreen>(app_, nullptr));
    }));
}

void ChestScreen::activate(const std::string& b) {
    if (!buttons_live_ || !enabled_) return;
    save::Progress& pr = app_.progress();
    (void)pr;
    if (b == "button_cards") { app_.sound().play_sound("chest_button", false, -1); set_tab(Tab::Cards); }
    else if (b == "button_belts") { app_.sound().play_sound("chest_button", false, -1); set_tab(Tab::Belts); }
    else if (b == "button_coins") { app_.sound().play_sound("chest_button", false, -1); set_tab(Tab::Coins); }
    else if (b == "button_flags") { app_.sound().play_sound("chest_button", false, -1); set_tab(Tab::Flags); }
    else if (b == "button_done") { app_.sound().play_sound("button_click", false, -1); done_clicked(); }
    else if (b == "cHelpButton") { app_.sound().play_sound("button_click", false, -1); show_help(); }
    else if (b == "button_scrollup") { app_.sound().play_sound("button_click", false, -1); set_grid_scroll(row_offset_ - 1); update_scroll_bar(); }
    else if (b == "button_scrolldn") { app_.sound().play_sound("button_click", false, -1); set_grid_scroll(row_offset_ + 1); update_scroll_bar(); }
    else if (b.rfind("beltbtn_", 0) == 0) { belt_clicked(std::atoi(b.c_str() + 8)); }
    else if (b.rfind("cShortcut_", 0) == 0) {
        app_.sound().play_sound("button_click", false, -1);
        flow::minigame_from_chest(app_, std::atoi(b.c_str() + 10), [this]() { build_coin_stacks(); });
    }
}

// ------------------------------------------------------------------------------------------------ cards tab

Point ChestScreen::belt_holder_pos(int i) const {
    int total = num_cards_ * 70;
    return {static_cast<float>(static_cast<int>(20 + sbso::kDesignW / 2.0f - total / 2.0f + 35 + 88)) + 70.0f * i, static_cast<float>(static_cast<int>(sbso::kDesignH - 43))};
}

Point ChestScreen::grid_holder_pos(int i) const {
    float w = kCardW + 2, h = kCardH + 5;  // CardSlot is a little larger than the card
    if (holder_lib_) {
        auto slot = MovieClip::create(holder_lib_, holder_lib_->symbol_id("CardHolder.CardSlot"), nullptr);
        Rect b = slot->bounds();
        w = b.w(); h = b.h();
    }
    return {static_cast<float>(static_cast<int>(337 + w * (i % kVisibleCols))), static_cast<float>(static_cast<int>(23 + h * (i / kVisibleCols)))};
}

void ChestScreen::set_num_cards() {
    const auto& belts = app_.config().belts;
    int bi = app_.progress().current_belt_index;
    num_cards_ = bi >= 0 && bi < static_cast<int>(belts.size()) ? std::min(5, belts[bi].capacity) : 3;
}

void ChestScreen::create_cards() {
    for (auto& t : all_cards_) t.clear();
    display_.clear();
    belt_holders_.clear();
    set_num_cards();
    const auto& db = app_.assets().db();
    save::Progress& pr = app_.progress();
    if (holder_lib_ && lib_)
        for (int i = 0; i < 5; ++i) {
            auto h = MovieClip::create(lib_, lib_->symbol_id("mc_cardslot_bottom"), &app_);
            belt_holders_.push_back(std::move(h));
        }
    intro_counter_ = 0;
    for (int t = 0; t < 4; ++t) {
        belt_slots_[t].assign(num_cards_, 0);
        new_snapshot_[t] = pr.new_cards[t];
        std::vector<std::string> belted = pr.belted[t];
        int row = 0, col = 0, slot = 0, idx = 0;
        for (const std::string& name : pr.all_cards[t]) {
            auto c = std::make_unique<DeckCard>();
            const auto* info = db.card(name);
            c->name = name;
            c->type = t;
            c->symbol = info ? info->symbol : "";
            c->cost = info ? std::to_string(info->cost) : "";
            c->power = info ? std::to_string(t == 0 ? info->max : info->damage) : "";
            if (card_lib_) {
                int img = card_lib_->symbol_id("cards." + c->symbol);
                if (img < 0 && c->symbol == "wammy") img = card_lib_->symbol_id("cards.whammy");
                if (img < 0) img = card_lib_->symbol_id("cards.unavailableCard");
                c->image = img;
            }
            const engine::Library* xl = app_.assets().library("cardXtras");
            if (xl) {
                c->flip = MovieClip::create(xl, xl->symbol_id(std::string("cards.flip_") + kTypeNames[t]), &app_);
                if (c->flip) c->flip->stop();
                c->cross = MovieClip::create(xl, xl->symbol_id("cards.cross_hair"), &app_);
                if (std::find(new_snapshot_[t].begin(), new_snapshot_[t].end(), name) != new_snapshot_[t].end()) {
                    c->sheen = MovieClip::create(xl, xl->symbol_id("cards.sheen"), &app_);
                    c->is_new = true;
                }
            }
            c->row = row;
            c->col = col;
            if (++col % kVisibleCols == 0) { col = 0; ++row; }
            auto it = std::find(belted.begin(), belted.end(), name);
            if (it != belted.end() && slot < num_cards_) {  // Chest_BattleDeckScreen.cardInPfBelt
                belted.erase(it);
                belt_slots_[t][slot] = 1;
                Point hp = belt_holder_pos(slot);
                c->x = hp.x - kCardW * 0.5f;
                c->y = hp.y - kCardH * 0.5f;
                c->in_deck = slot++;
                if (t == 0) { move_in(*c, false, idx); ++intro_counter_; }
            } else if (c->index(kVisibleCols) < kVisibleCols * kVisibleRows) {
                Point gp = grid_holder_pos(idx);
                c->x = gp.x;
                c->y = gp.y;
                c->in_deck = -1;
                if (t == 0) { move_in(*c, true, idx); ++intro_counter_; }
            }
            all_cards_[t].push_back(std::move(c));
            ++idx;
        }
    }
    enabled_ = intro_counter_ == 0;
}

void ChestScreen::move_in(DeckCard& c, bool from_top, int index) {
    c.dest = {static_cast<float>(static_cast<int>(c.x)), static_cast<float>(static_cast<int>(c.y))};
    c.y = from_top ? -100.0f : sbso::kDesignH + 100.0f;
    c.spring = -index * 2;
    c.st = DeckCard::St::MoveIn;
    c.yf = c.dest.y - c.y;
}

int ChestScreen::max_row_offset() const {
    if (card_type_ < 0) return 0;
    int n = static_cast<int>(all_cards_[card_type_].size());
    return std::max(0, (n - kVisibleCols * kVisibleRows + kVisibleCols - 1) / kVisibleCols);
}

void ChestScreen::set_card_type(int type) {
    for (DeckCard* c : display_) c->displayed = false;
    display_.clear();
    int n = static_cast<int>(all_cards_[type].size());
    for (int i = 0; i < n; ++i) {
        DeckCard* c = all_cards_[type][i].get();
        int slot = c->index(kVisibleCols) - row_offset_ * kVisibleCols;
        if ((slot < kVisibleCols * kVisibleRows && slot >= 0) || c->in_deck != -1) { display_.push_back(c); c->displayed = true; }
    }
    viewed_[type] = true;
    card_type_ = type;
}

void ChestScreen::scroll_grid() {
    if (card_type_ < 0) return;
    for (auto& up : all_cards_[card_type_]) {
        DeckCard* c = up.get();
        if (c->in_deck != -1) continue;
        int slot = c->index(kVisibleCols) - row_offset_ * kVisibleCols;
        bool vis = slot >= 0 && slot < kVisibleCols * kVisibleRows;
        if (c->displayed && !vis) {
            c->displayed = false;
            display_.erase(std::remove(display_.begin(), display_.end(), c), display_.end());
            if (hovered_ == c) hovered_ = nullptr;
        } else if (vis) {
            Point gp = grid_holder_pos(slot);
            c->x = gp.x;
            c->y = gp.y;
            if (!c->displayed) { c->displayed = true; display_.push_back(c); }
        }
    }
}

void ChestScreen::set_grid_scroll(int offset) {
    row_offset_ = std::max(0, std::min(max_row_offset(), offset));
    scroll_grid();
}

void ChestScreen::update_scroll_bar() {
    MovieClip* c = panel_.clip();
    if (!c || tab_ != Tab::Cards) return;
    int mx = max_row_offset();
    if (mx) {
        float r = static_cast<float>(row_offset_) / mx;
        float bx = 0, by = 0;
        c->get_child_xy("button_scrollbar", &bx, &by);
        c->set_child_xy("button_scrollbar", bx, kScrollMin + (kScrollMax - kScrollMin) * r);
    }
    for (const char* n : {"button_scrollup", "button_scrolldn", "button_scrollbar"}) c->set_child_visible(n, mx > 0);
}

void ChestScreen::change_card_type(int type, bool from_carousel) {
    row_offset_ = 0;
    set_card_type(type);
    scroll_grid();
    if (from_carousel) {
        if (radio_) {
            const char* names[4] = {"cMove", "cAttack", "cDefend", "cNik"};
            for (int i = 0; i < 4; ++i)
                if (MovieClip* c = radio_->child(names[i])) c->goto_and_stop(i == type ? 2 : 1);
        }
    } else if (carousel_) {
        carousel_->set_type(type);
    }
    if (MovieClip* d = panel_.clip() ? panel_.clip()->child("cDiscription") : nullptr) d->goto_label_and_stop(kTypeNames[type]);
    update_scroll_bar();
}

void ChestScreen::hide_range() {
    range_clips_.clear();
    range_pos_.clear();
    range_visible_ = false;
    if (MovieClip* d = panel_.clip() ? panel_.clip()->child("cDiscription") : nullptr) d->set_visible(true);
}

void ChestScreen::show_range(int type, const std::string& card) {
    const auto* info = app_.assets().db().card(card);
    if (!info) return;
    if (type == 3 && info->effect == "HEAL") return;
    hide_range();
    if (type == 2) return;
    if (MovieClip* d = panel_.clip() ? panel_.clip()->child("cDiscription") : nullptr) d->set_visible(false);
    range_visible_ = true;  // the description text hides while a range is shown (DEFEND keeps it)
    int mn = info->min, mx = info->max;
    const char* sym = type == 0 ? "rangebox_small_MOVE" : type == 1 ? "rangebox_small_ATTACK" : "rangebox_small_NICK";
    auto add = [&](int x, int y) {
        if (std::abs(x) > 7 || std::abs(y) > 6 || !lib_) return;
        range_clips_.push_back(MovieClip::create(lib_, lib_->symbol_id(sym), &app_));
        range_pos_.push_back({static_cast<float>(x) * 10 + 160.5f + 50, static_cast<float>(y) * 7.98f - 46.5f + 350});
    };
    if (type == 0) {
        for (int dx = -mx; dx <= mx; ++dx)
            for (int dy = -mx; dy <= mx; ++dy)
                if ((dx || dy) && std::abs(dx) + std::abs(dy) <= mx) add(dx, dy);
    } else {
        for (int i = -mx; i <= mx; ++i) {
            int lo = i < 0 ? -(mx + i) : -(mx - i), hi = i < 0 ? mx + i : mx - i, ai = std::abs(i);
            for (int j = lo; j <= hi; ++j)
                if (ai + std::abs(j) > mn) add(i, j);
        }
    }
}

void ChestScreen::card_roll_over(DeckCard& c) {
    app_.sound().play_sound("battle_card_rollover", false, -1);
    if (c.st != DeckCard::St::Move) {
        c.loc = 2;
        c.hover = true;
        show_range(c.type, c.name);
    }
}

void ChestScreen::card_roll_out(DeckCard& c) {
    c.loc = 1;
    c.hover = false;
    hide_range();
}

void ChestScreen::card_click(DeckCard& c) {
    if (c.sheen) {
        auto& nc = app_.progress().new_cards[c.type];  // Profile.makeCardOld
        auto it = std::find(nc.begin(), nc.end(), c.name);
        if (it != nc.end()) nc.erase(it);
        c.sheen.reset();
    }
    if (c.in_deck != -1) {
        c.hover = false;
        app_.sound().play_sound("chest_remove_card_from_belt", false, -1);
        if (c.row < row_offset_) { set_grid_scroll(c.row); update_scroll_bar(); }
        else if (c.row >= row_offset_ + kVisibleRows) { set_grid_scroll(c.row + 1 - kVisibleRows); update_scroll_bar(); }
        belt_slots_[card_type_][c.in_deck] = 0;  // vacateBelt
        c.in_deck = -1;
        Point gp = grid_holder_pos(c.col + (c.row - row_offset_) * kVisibleCols);
        c.dest = {static_cast<float>(static_cast<int>(gp.x)), static_cast<float>(static_cast<int>(gp.y))};
        c.st = DeckCard::St::Move;
        c.loc = 1; c.xf = c.yf = 0; c.spring = 0;
        c.dir = c.dest.y - c.y > 1 ? 1 : -1;
        hide_range();
    } else {
        int free_slot = -1;
        for (int i = 0; i < num_cards_; ++i)
            if (belt_slots_[card_type_][i] == 0) { free_slot = i; break; }
        if (free_slot >= 0) {
            belt_slots_[card_type_][free_slot] = 1;
            app_.sound().play_sound("chest_add_card_to_belt", false, -1);
            Point hp = belt_holder_pos(free_slot);
            c.dest = {static_cast<float>(static_cast<int>(hp.x - kCardW * 0.5f)), static_cast<float>(static_cast<int>(hp.y - kCardH * 0.5f))};
            c.in_deck = free_slot;
            c.st = DeckCard::St::Move;
            c.loc = 1; c.xf = c.yf = 0; c.spring = 0; c.hover = false;
            c.dir = c.dest.y - c.y > 1 ? 1 : -1;
            hide_range();
        } else {
            app_.sound().play_sound("i_clicknegative", false, -1);
            c.st = DeckCard::St::Shake;
            c.xf = c.x;
            c.yf = 0;
            c.loc = 1; c.hover = false;
            hide_range();
        }
    }
}

bool ChestScreen::click_card(int type, const std::string& name) {
    if (tab_ != Tab::Cards || !enabled_) return false;
    if (card_type_ != type) change_card_type(type, false);
    for (auto& c : all_cards_[type])
        if (c->name == name && c->displayed && c->st == DeckCard::St::Idle) { card_click(*c); return true; }
    return false;
}

void ChestScreen::tick_card(DeckCard& c) {
    switch (c.st) {
        case DeckCard::St::MoveIn:
            if (++c.spring > 0) {
                float a = kPi * 0.65f, b = a * c.spring / 9;
                if (b >= a) {
                    c.y = c.dest.y;
                    c.st = DeckCard::St::Idle;
                    if (--intro_counter_ == 0) { enabled_ = true; intro_done_ = true; }
                } else {
                    float k = 1 / std::sin(a);
                    c.y = c.dest.y + c.yf * (std::sin(b) * k - 1);
                    if (std::fabs(c.yf) > 170) c.yf = c.dest.y - c.y;
                }
            }
            break;
        case DeckCard::St::Move: {
            c.xf += (c.dest.x - c.x) * 0.4f;
            c.xf *= 0.7f;
            c.x += c.xf;
            c.yf += (c.dest.y - c.y) * 0.4f;
            c.yf *= 0.7f;
            c.y += c.yf;
            int cur = c.dest.y - c.y > 1 ? 1 : -1;
            if (c.dir != cur || c.dest.y - c.y == 0) {
                ++c.spring;
                c.dir = cur;
                if (c.spring > 4) { c.x = c.dest.x; c.y = c.dest.y; c.st = DeckCard::St::Idle; }
            }
            break;
        }
        case DeckCard::St::Shake:
            c.yf += 1;
            if (c.yf > kPi * 2) { c.yf = kPi * 2; c.st = DeckCard::St::Idle; c.x = c.xf; }
            else c.x = c.xf + std::sin(c.yf) * 5;
            break;
        case DeckCard::St::Idle:
            break;
    }
}

ChestScreen::DeckCard* ChestScreen::card_at(Point p) {
    for (auto it = display_.rbegin(); it != display_.rend(); ++it) {
        DeckCard* c = *it;
        if (c->st != DeckCard::St::Idle) continue;
        float by = c->loc == 2 ? -5.0f : 0.0f;
        Rect r{kBds.x + c->x, kBds.y + c->y + by, kBds.x + c->x + kCardW, kBds.y + c->y + by + kCardH};
        if (inside(r, p)) return c;
    }
    return nullptr;
}

void ChestScreen::remove_extra_cards_from_belt() {
    save::Progress& pr = app_.progress();
    set_num_cards();
    for (int t = 0; t < 4; ++t)
        while (static_cast<int>(pr.belted[t].size()) > num_cards_) pr.belted[t].pop_back();
}

// ------------------------------------------------------------------------------------------------ belts tab

void ChestScreen::build_belt_tab() {
    MovieClip* c = panel_.clip();
    if (!c || !lib_) return;
    const auto& belts = app_.config().belts;
    const save::Progress& pr = app_.progress();
    current_belt_ = belt_shown_ = pr.current_belt_index;
    belt_buttons_.assign(10, nullptr);
    belt_shadows_.assign(10, nullptr);
    MovieClip* shadows = c->child("cBeltShadowHolder");
    MovieClip* buttons = c->child("cBeltButtonHolder");
    for (int i = 0; i < 10 && i < static_cast<int>(belts.size()); ++i) {
        const std::string& n = belts[i].name;
        if (shadows) {
            if (MovieClip* slot = shadows->child("cBeltShadow_" + std::to_string(i))) {
                auto sh = MovieClip::create(lib_, lib_->symbol_id("holder_" + n), &app_);
                if (sh) {
                    if (i != current_belt_) sh->goto_and_stop(2);
                    else sh->goto_and_stop(1);
                    belt_shadows_[i] = slot->add_child_clip(std::move(sh));
                }
            }
        }
        if (pr.gained_belts[i] && buttons) {
            if (MovieClip* slot = buttons->child("cBelt_" + std::to_string(i))) {
                auto bt = MovieClip::create_any(lib_, lib_->symbol_id("button_belt_" + n), &app_);
                if (bt) {
                    bt->set_name("beltbtn_" + std::to_string(i));
                    if (i == current_belt_) bt->set_visible(false);
                    belt_buttons_[i] = slot->add_child_clip(std::move(bt));
                }
            }
        }
    }
}

void ChestScreen::change_belt_display(int belt, bool temp) {
    MovieClip* c = panel_.clip();
    const auto& belts = app_.config().belts;
    if (!c || belt < 0 || belt >= static_cast<int>(belts.size())) return;
    belt_shown_ = belt;
    const std::string& n = belts[belt].name;
    if (MovieClip* m = c->child("belt_equiped")) m->goto_label_and_stop(n);
    if (MovieClip* m = c->child("belt_stats")) {
        m->goto_label_and_stop(n);
        const auto& bi = belts[belt];
        if (MovieClip* caps = m->child("cCaps")) caps->goto_and_stop(bi.capacity);
        if (MovieClip* top = m->child("cTopNumbers")) top->goto_label_and_stop("num" + std::to_string(bi.plus_health));
        if (MovieClip* bot = m->child("cBottomNumbers")) bot->goto_label_and_stop("num" + std::to_string(bi.plus_energy));
    }
    if (MovieClip* m = c->child("belt_explanations")) m->goto_label_and_stop((temp ? "_" : "") + n);
}

void ChestScreen::belt_clicked(int i) {
    if (i < 0 || i >= 10 || i == current_belt_ || !belt_buttons_[i]) return;
    app_.sound().play_sound("button_click", false, -1);
    if (current_belt_ >= 0 && current_belt_ < 10) {
        if (belt_buttons_[current_belt_]) belt_buttons_[current_belt_]->set_visible(true);
        if (belt_shadows_[current_belt_]) belt_shadows_[current_belt_]->goto_and_stop(2);
    }
    belt_buttons_[i]->set_visible(false);
    if (belt_shadows_[i]) belt_shadows_[i]->goto_and_stop(1);
    current_belt_ = i;
    app_.progress().current_belt_index = i;
    change_belt_display(i, false);
    remove_extra_cards_from_belt();
}

// ------------------------------------------------------------------------------------------------ coins / flags

void ChestScreen::build_coin_stacks() {
    // Chest_CoinScreen.createAllCoinStacks (same pseudo random sequence)
    static const double kRand[13] = {0.9, 0.7, 0.85, 0.4, 0.6, 0.45, 0.95, 0.57, 0.67, 0.88, 0.3, 0.45, 0.69};
    int counter = 0;
    stacks_.clear();
    int coins = std::min(app_.progress().coins, 500);
    const double cx = 400, cy = 280, r = 32;
    int ring = 0, in_ring = 0;
    double per = std::min(50.0, coins / 5.0);
    while (coins > 0) {
        int n;
        if (coins > 50) { counter = (counter + 1) % 13; n = static_cast<int>(kRand[counter] * per); }
        else n = static_cast<int>(per);
        n = std::max(1, std::min(coins, n));
        coins -= n;
        Stack s{0, 0, n};
        double rad = ring * r;
        int slots = static_cast<int>(std::floor(2 * kPi * rad / r * 0.85));
        if (slots > 0) {
            double a = static_cast<double>(in_ring) / slots * kPi * 2 - kPi / 6;
            s.x = static_cast<float>(cx + rad * std::cos(a));
            s.y = static_cast<float>(cy + rad * std::sin(a) * 0.6);
            per = std::max(5.0, std::floor(per * 0.95));
        } else {
            s.x = static_cast<float>(cx);
            s.y = static_cast<float>(cy);
            per *= 0.92;
        }
        if (++in_ring >= slots * 0.66) { in_ring = 0; ++ring; }
        stacks_.push_back(s);
    }
    std::stable_sort(stacks_.begin(), stacks_.end(), [](const Stack& a, const Stack& b) { return a.y < b.y; });
    if (gui_lib_ && !coin_) {
        coin_ = MovieClip::create(gui_lib_, gui_lib_->symbol_id("Coin"), &app_);
        if (coin_) coin_->goto_and_stop(1);
    }
}

void ChestScreen::update_map_flags() {
    MovieClip* c = panel_.clip();
    if (!c) return;
    const save::Progress& pr = app_.progress();
    for (int s = 0; s < 9; ++s) {
        int cleared = 0;
        for (int l = 0; l < 6; ++l)
            if (pr.unlocked_stages[s][l] == save::kCleared) ++cleared;
        if (MovieClip* area = c->child("flag_area_" + std::to_string(s + 1))) {
            if (MovieClip* n = area->child("num_flags")) n->goto_and_stop(cleared + 1);
            if (MovieClip* t = area->child("total_flags")) t->goto_and_stop(7);
        }
        if (MovieClip* tr = c->child("cTrophy" + std::to_string(s + 1)))
            if (MovieClip* t = tr->child("cTrophy")) t->set_visible(cleared == 6);
    }
}

// ------------------------------------------------------------------------------------------------ frame / input

void ChestScreen::tick(App&) {
    if (!panel_.valid()) return;
    ++timer_;
    tab_update();
    panel_.tick();
    menu_buttons_.tick();
    if (tab_ == Tab::Cards) {
        for (auto& t : all_cards_)
            for (auto& c : t) {
                tick_card(*c);
                if (c->sheen) c->sheen->tick();
            }
        if (carousel_) {
            carousel_->tick();
            if (carousel_timer_ <= 10) {
                ++carousel_timer_;
            }
        }
        for (auto& h : belt_holders_)
            if (h) h->tick();
        // hover (rollOver / rollOut as cards settle)
        DeckCard* over = enabled_ ? card_at(pointer_) : nullptr;
        if (over != hovered_) {
            if (hovered_) card_roll_out(*hovered_);
            hovered_ = over;
            if (over) card_roll_over(*over);
        }
        if (dragging_bar_) {
            int mx = max_row_offset();
            float y = std::min(kScrollMax, std::max(kScrollMin, pointer_.y - sbso::kDesignH / 2.0f));
            panel_.clip()->set_child_xy("button_scrollbar", 295.5f, y);
            float r = (y - kScrollMin) / (kScrollMax - kScrollMin);
            set_grid_scroll(static_cast<int>(std::lround(r * mx)));
        }
    }
}

void ChestScreen::pointer_move(App& app, Point p) {
    pointer_ = p;
    menu_buttons_.pointer_move(app, p);
    if (buttons_live_ && enabled_) panel_.pointer_move(app, p);
}

void ChestScreen::pointer_down(App& app, Point p) {
    pointer_ = p;
    menu_buttons_.pointer_down(app, p);
    if (!buttons_live_ || !enabled_) return;
    panel_.pointer_down(app, p);
    if (tab_ == Tab::Cards) {
        if (carousel_ && carousel_timer_ > 10) carousel_->pointer_down({p.x - (kBds.x + 196), p.y - (kBds.y + 435)});
        // type radio buttons (BDeckScreen_RadioSet at BDS (108,121))
        if (radio_) {
            const char* names[4] = {"cMove", "cAttack", "cDefend", "cNik"};
            std::vector<engine::ButtonHit> hits;
            radio_->collect_buttons(hits, Matrix{1, 0, 0, 1, kBds.x + 108, kBds.y + 121});
            for (auto& h : hits)
                if (h.contains(p))
                    for (int i = 0; i < 4; ++i)
                        if (h.owner->in_named(names[i])) {
                            app_.sound().play_sound("chest_button", false, -1);
                            app_.sound().play_sound("i_carousel", false, -1);
                            change_card_type(i, false);
                            for (int k = 0; k < 4; ++k)
                                if (MovieClip* c = radio_->child(names[k])) c->goto_and_stop(k == i ? 2 : 1);
                            return;
                        }
        }
        // scroll bar drag
        std::vector<engine::ButtonHit> hits;
        panel_.clip()->collect_buttons(hits, place_);
        for (auto& h : hits)
            if (h.name == "button_scrollbar" && h.contains(p) && max_row_offset() > 0) dragging_bar_ = true;
    }
}

void ChestScreen::pointer_up(App& app, Point p) {
    pointer_ = p;
    dragging_bar_ = false;
    if (menu_buttons_.pointer_up(app, p) == "cMenuButt") { open_menu(); return; }
    if (!buttons_live_ || !enabled_) return;
    std::string b = panel_.pointer_up(app, p);
    if (!b.empty() && b != "button_scrollbar") { activate(b); return; }
    if (tab_ == Tab::Cards) {
        if (DeckCard* c = card_at(p)) {
            if (c == hovered_) card_click(*c);
        }
    } else if (tab_ == Tab::Belts && belt_hover_ >= 0) {
        // handled through the beltbtn_ buttons
    }
}

void ChestScreen::key_down(App&, int key) {
    if (key == 27) open_menu();
}

void ChestScreen::draw(App&, std::vector<engine::DrawCmd>& out) {
    if (!panel_.valid()) return;
    panel_.draw(out);
    static const float kWhite[4] = {1, 1, 1, 1};
    if (tab_ == Tab::Cards) {
        // grid slots (alpha 0.2 black) and belt holders
        if (holder_lib_) {
            engine::ColorTransform slotcx;
            slotcx.mult[0] = slotcx.mult[1] = slotcx.mult[2] = 0;
            slotcx.mult[3] = 0.2f;
            static std::unique_ptr<MovieClip> slot;
            if (!slot) slot = MovieClip::create(holder_lib_, holder_lib_->symbol_id("CardHolder.CardSlot"), nullptr);
            if (slot)
                for (int i = 0; i < kVisibleCols * kVisibleRows; ++i) {
                    Point gp = grid_holder_pos(i);
                    slot->collect(out, Matrix{1, 0, 0, 1, kBds.x + gp.x, kBds.y + gp.y}, slotcx);
                }
        }
        for (int i = 0; i < num_cards_ && i < static_cast<int>(belt_holders_.size()); ++i) {
            Point hp = belt_holder_pos(i);
            if (belt_holders_[i]) belt_holders_[i]->collect(out, Matrix{1, 0, 0, 1, kBds.x + hp.x, kBds.y + hp.y}, engine::ColorTransform{});
        }
        if (radio_) radio_->collect(out, Matrix{1, 0, 0, 1, kBds.x + 108, kBds.y + 121}, engine::ColorTransform{});
        if (carousel_) {
            float t = std::min(1.0f, static_cast<float>(carousel_timer_) / 10.0f);
            float s = std::sin(carousel_timer_ / 8.0f * kPi / 2) / std::sin(10 / 8.0f * kPi / 2);
            carousel_->draw(out, Matrix{1, 0, 0, 1, kBds.x + 196, kBds.y + 435}, carousel_timer_ >= 10 ? 1.0f : std::max(0.0f, s) * (t > 0 ? 1 : 0));
        }
        for (DeckCard* c : display_) {
            float by = c->loc == 2 ? -5.0f : (c->loc == 3 ? -10.0f : 0.0f);
            Matrix cm{1, 0, 0, 1, kBds.x + c->x, kBds.y + c->y};
            engine::DrawCmd d;
            if (engine::image_cmd(card_lib_, c->image, cm * Matrix{1, 0, 0, 1, 0, by}, &d)) out.push_back(d);
            if (c->flip) c->flip->collect(out, cm, engine::ColorTransform{});
            out.push_back(engine::text_cmd("unibody8black", 8, kWhite, c->cost, cm, 18, 72 + by));
            out.push_back(engine::text_cmd("unibody8black", 8, kWhite, c->power, cm, 43, 72 + by));
            if (c->sheen) c->sheen->collect(out, cm * Matrix{1, 0, 0, 1, 0, by}, engine::ColorTransform{});
            if (c->hover && c->cross) c->cross->collect(out, cm * Matrix{1, 0, 0, 1, 0, by}, engine::ColorTransform{});
        }
        for (size_t i = 0; i < range_clips_.size(); ++i)
            range_clips_[i]->collect(out, Matrix{1, 0, 0, 1, kBds.x + range_pos_[i].x, kBds.y + range_pos_[i].y}, engine::ColorTransform{});
    } else if (tab_ == Tab::Coins && coin_) {
        for (const Stack& s : stacks_)
            for (int i = 0; i < s.coins; ++i) coin_->collect(out, Matrix{1, 0, 0, 1, s.x, s.y - i * 5.0f}, engine::ColorTransform{});
    }
    menu_buttons_.draw(out);
}

}  // namespace sbso::app
