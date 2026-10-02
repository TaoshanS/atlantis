#include "minigame_screen.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "engine/draw_util.h"

namespace sbso::app {
using engine::Matrix;
using engine::MovieClip;
using engine::Point;
using engine::Rect;

namespace {

constexpr int kCardW = 68, kCardH = 85;
const char* kLeftHeads[] = {"sb_awe", "unhappySmirk", "whisper", "happy", "mesmerized", "bigsmile", "yelling", "worried", "satisfied", "shocked"};
struct Rgb { int r, g, b; };
const Rgb kInner[11] = {{124, 139, 59}, {147, 55, 67}, {104, 104, 104}, {173, 123, 16}, {77, 134, 137}, {118, 86, 126}, {115, 72, 37}, {173, 123, 16}, {112, 169, 4}, {153, 153, 153}, {255, 255, 255}};
const Rgb kOuter[11] = {{166, 185, 80}, {193, 75, 92}, {154, 154, 154}, {224, 161, 25}, {108, 195, 196}, {152, 109, 159}, {64, 40, 21}, {224, 161, 25}, {81, 125, 1}, {51, 51, 51}, {255, 255, 255}};

int color_index(const std::string& head) {
    static const std::pair<const char*, int> kMap[] = {
        {"mr_krabs", 1}, {"plankton_terrified", 2}, {"plankton_yelling", 2}, {"plankton_sneaky", 2}, {"plankton_remote", 2}, {"plankton_smiling", 2},
        {"plankton_veryHappy", 2}, {"squidward_talk", 3}, {"squidward_exhausted", 3}, {"squidward_smile", 3}, {"squidward_unhappy", 3}, {"bubblebuddy", 4},
        {"patrick_happy", 5}, {"patrick_wave", 5}, {"patrick_drooling", 5}, {"bully_beach_atlantean", 5}, {"bubble_dirty", 6}, {"pirate_ghost", 7},
        {"atlantean_gaurd_smiling", 8}, {"atlantean_gaurd_angryEyes", 8}, {"atlantean_gaurd_bigSmile", 8}, {"atlantean_king_satisfied", 8},
        {"atlantean_king_talking", 8}, {"manray_forward", 9}, {"manray_backward", 9}};
    for (const auto& p : kMap)
        if (head == p.first) return p.second;
    return 10;
}

bool is_left(const std::string& h) {
    for (const char* l : kLeftHeads)
        if (h == l) return true;
    return false;
}

float cos_interp(float a, float b, float t) {
    float k = (1 - std::cos(t * 3.14159265f)) / 2;
    return a * (1 - k) + b * k;
}
float lin_interp(float a, float b, float t) { return a * (1 - t) + b * t; }

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
int rnd(int n) { return n <= 0 ? 0 : static_cast<int>(rng()() % static_cast<unsigned>(n)); }

}  // namespace

struct MinigameScreen::Card {
    enum class St { MoveIn, FaceDown, Mix, Face, Idle } st = St::Idle;
    std::string name;
    int type = 3;
    int image = -1;
    std::string cost, power;
    bool whammy = false, face_up = true, mouse = false, hover = false, mixed = false;
    float x = -320, y = 0, prog = 0, inc = 0;
    Point origin, dest;
    std::unique_ptr<MovieClip> flip, cross;
    int index = 0;
};

struct MinigameScreen::Grid {
    enum class State { MoveIn, FaceUp, Mix, Idle } state = State::MoveIn;
    std::vector<std::unique_ptr<Card>> cards;                    // owner, in creation order
    std::vector<Card*> slots;                                    // current arrangement (swapped while mixing)
    int cols = 2, rows = 2, spacing = 10;
    int timer = 0, counter = 0, card_index = 0, cur_col = 0, cur_row = 0, intro_interval = 4, flip_interval = 1;
    std::vector<Card*> to_mix;
    bool mix_done = false;
};

MinigameScreen::MinigameScreen(App& app, int game, std::function<void()> on_done) : app_(app), game_(game), on_done_(std::move(on_done)) {
    const engine::Library* lib = app.assets().library("minigame");
    if (lib) panel_ = ClipPanel(MovieClip::create(lib, lib->symbol_id("mingame"), &app), Matrix{1, 0, 0, 1, sbso::kDesignW / 2.0f, sbso::kDesignH / 2.0f}, {"cYES", "cNO"});
    if (game_ >= 0 && game_ < 4) ++app.progress().minigame_plays[game_];  // Profile.playedMiniGame
    if (!panel_.valid()) phase_ = Phase::Done;
}

MinigameScreen::~MinigameScreen() = default;

bool MinigameScreen::cards_ready() const { return grid_ && grid_->state == Grid::State::Idle && !disabled_; }

int MinigameScreen::num_cards() const { return grid_ ? static_cast<int>(grid_->cards.size()) : 0; }

bool MinigameScreen::has_enough_coins() const {
    const auto& mg = app_.config().minigames;
    return game_ < static_cast<int>(mg.size()) && app_.progress().coins >= mg[game_].cost;
}

void MinigameScreen::set_head(const std::string& frame, const std::string& text) {
    if (!dialogue_) return;
    const engine::Library* heads = app_.assets().library("Heads");
    bool left = is_left(frame);
    if (heads) {
        auto make = [&](const char* cls) {
            auto c = MovieClip::create(heads, heads->symbol_id(cls), &app_);
            if (auto* hs = c->child("cHeads")) {
                hs->goto_label_and_stop(frame);
                if (auto* h = hs->child("cHead")) h->goto_and_play(1);
            }
            return c;
        };
        if (left) { head_l_ = make("LeftHeads"); head_r_.reset(); }
        else { head_r_ = make("RightHeads"); head_l_.reset(); }
    }
    int ci = left ? 0 : color_index(frame);
    dialogue_->set_child_tint("cBGFill", kOuter[ci].r, kOuter[ci].g, kOuter[ci].b);
    dialogue_->set_child_tint("cInnerFill", kInner[ci].r, kInner[ci].g, kInner[ci].b);
    float x = 0, y = 0;
    dialogue_->get_child_xy("cInnerFill", &x, &y);
    float nx = left ? -103.0f : -233.0f;
    dialogue_->set_child_xy("cInnerFill", nx, y);
    dialogue_->set_child_xy("cMask", nx, 82);
    float ty = 73;
    dialogue_->get_child_xy("cText", &x, &ty);
    dialogue_->set_child_xy("cText", nx + 15, 78);
    dialogue_->set_text("cText", text);
}

void MinigameScreen::show_line(int index) {
    const auto& dl = app_.dialogues().minigame;
    if (game_ >= static_cast<int>(dl.size()) || index >= static_cast<int>(dl[game_].size())) return;
    const auto& l = dl[game_][index];
    set_head(l.frame, app_.tr(l.key, l.text));
}

void MinigameScreen::init_yes_no(bool on) { buttons_on_ = on; }

void MinigameScreen::show_too_poor() {
    timer_ = 0;
    phase_ = panel_.clip()->current_label() == "dialogue" ? Phase::TooPoorBackwards : Phase::TooPoorForwards;
}

void MinigameScreen::yes_clicked() {
    app_.sound().play_sound("button_click", false, -1);
    init_yes_no(false);
    disabled_ = true;
    const auto& mg = app_.config().minigames;
    save::add_coins(app_.progress(), -mg[game_].cost);  // payUp
    show_line(1);
    MovieClip* mc = panel_.clip();
    if (mc->current_label() == "dialogue") {
        mc->play();
        phase_ = Phase::LoadCards;
        timer_ = -1000;  // wait for the "minigame" label first (playToMinigame)
    } else {
        mc->goto_label_and_stop("minigame");
        timer_ = 0;
        phase_ = Phase::LoadCards;
    }
}

void MinigameScreen::no_clicked() {
    app_.sound().play_sound("button_click", false, -1);
    init_yes_no(false);
    disabled_ = true;
    MovieClip* mc = panel_.clip();
    if (mc->current_label() == "dialogue") {
        phase_ = Phase::ExitBackwards;
    } else {
        mc->play();
        phase_ = Phase::ExitForwards;
    }
}

void MinigameScreen::load_cards() {
    const auto& mg = app_.config().minigames[game_];
    int n = mg.num_cards;
    std::vector<std::string> pool(n), arranged(n);
    bool rare = rnd(2) == 1;
    for (int i = 0; i < mg.whammies && i < n; ++i) pool[i] = "w";
    if (mg.whammies < n) {
        const auto& src = rare && !mg.rares.empty() ? mg.rares : mg.commons;
        pool[mg.whammies] = src[rnd(static_cast<int>(src.size()))];
    }
    for (int i = mg.whammies + 1; i < n; ++i) pool[i] = mg.commons[rnd(static_cast<int>(mg.commons.size()))];
    std::vector<bool> used(n, false);
    for (int i = 0; i < n; ++i) {
        int j = rnd(n);
        while (used[j]) j = rnd(n);
        used[j] = true;
        arranged[j] = pool[i];
    }
    grid_ = std::make_unique<Grid>();
    grid_->rows = 2;
    grid_->cols = n / 2;
    const engine::Library* card_lib = app_.assets().library("cardAssets");
    const engine::Library* xl = app_.assets().library("cardXtras");
    const auto& db = app_.assets().db();
    for (int i = 0; i < n; ++i) {
        auto c = std::make_unique<Card>();
        c->name = arranged[i];
        c->whammy = arranged[i] == "w";
        c->index = i;
        std::string sym = "whammy";
        if (!c->whammy) {
            const auto* info = db.card(c->name);
            if (info) {
                sym = info->symbol;
                c->type = static_cast<int>(info->type);
                c->cost = std::to_string(info->cost);
                c->power = std::to_string(info->type == sbso::game::CardType::Move ? info->max : info->damage);
            }
        }
        if (card_lib) {
            c->image = card_lib->symbol_id("cards." + sym);
            if (c->image < 0) c->image = card_lib->symbol_id("cards.unavailableCard");
        }
        static const char* kFlip[4] = {"MOVE", "ATTACK", "DEFEND", "NICK"};
        if (xl) {
            c->flip = MovieClip::create(xl, xl->symbol_id(std::string("cards.flip_") + kFlip[c->whammy ? 3 : c->type]), &app_);
            if (c->flip) c->flip->stop();
            c->cross = MovieClip::create(xl, xl->symbol_id("cards.cross_hair"), &app_);
        }
        c->x = -320;
        c->y = 0;
        grid_->slots.push_back(c.get());
        grid_->cards.push_back(std::move(c));
    }
    grid_->state = Grid::State::MoveIn;
    phase_ = Phase::Playing;
}

void MinigameScreen::card_chosen(Card& c) {
    app_.sound().play_sound(c.whammy ? "minigame_whammy" : "battle_level_victory", false, -1);
    for (auto& k : grid_->cards) { k->mouse = false; k->hover = false; }
    chosen_ = c.name;
    disabled_ = true;
    if (!c.whammy) save::add_new_card(app_.progress(), c.type, c.name);
    timer_ = 0;
    phase_ = Phase::CardEarned;
}

void MinigameScreen::pick(int index) {
    if (!grid_ || index < 0 || index >= static_cast<int>(grid_->slots.size())) return;
    Card* c = grid_->slots[index];
    if (!c->mouse) return;
    c->mouse = false;
    c->face_up = true;
    c->st = Card::St::Face;
    card_chosen(*c);
}

void MinigameScreen::finish(App& app) {
    if (done_called_) return;
    done_called_ = true;
    app.close_overlay();
    if (on_done_) on_done_();
}

int MinigameScreen::phase_label_frame(const char* label) const { return panel_.clip()->frame_of_label(label); }

void MinigameScreen::tick_grid(App& app) {
    Grid& g = *grid_;
    // --- cards
    for (auto& cp : g.cards) {
        Card& c = *cp;
        if (c.st == Card::St::MoveIn || c.st == Card::St::Mix) {
            c.prog += c.inc;
            if (c.prog < 1) {
                c.x = lin_interp(c.origin.x, c.dest.x, c.prog);
                c.y = cos_interp(c.origin.y, c.dest.y, c.prog);
            } else {
                c.x = c.dest.x;
                c.y = c.dest.y;
                bool was_mix = c.st == Card::St::Mix;
                c.st = Card::St::Idle;
                if (!was_mix) {  // MOVEIN_DONE
                    if (++g.counter == static_cast<int>(g.cards.size())) {
                        g.state = Grid::State::FaceUp;
                        g.timer = g.counter = 0;
                        g.card_index = static_cast<int>(g.cards.size()) - 1;
                        g.flip_interval = 60;
                    }
                } else {  // MIX_DONE
                    if (++g.counter == static_cast<int>(g.cards.size()) * 4) {
                        disabled_ = false;
                        for (auto& k : g.cards) k->mouse = true;
                        g.state = Grid::State::Idle;
                    } else if (g.counter % 2 == 0) {
                        // next pair
                        auto idle = [&](int i) { return g.slots[i]->st == Card::St::Idle; };
                        if (!g.to_mix.empty()) {
                            int pick_i = rnd(static_cast<int>(g.to_mix.size()));
                            Card* a = g.to_mix[pick_i];
                            int ai = static_cast<int>(std::find(g.slots.begin(), g.slots.end(), a) - g.slots.begin());
                            std::vector<int> nb;
                            if ((ai + 1) % g.cols != 0 && idle(ai + 1)) nb.push_back(ai + 1);
                            if (ai % g.cols != 0 && idle(ai - 1)) nb.push_back(ai - 1);
                            if (ai >= g.cols && idle(ai - g.cols)) nb.push_back(ai - g.cols);
                            if (ai < static_cast<int>(g.cards.size()) - g.cols && idle(ai + g.cols)) nb.push_back(ai + g.cols);
                            if (!nb.empty()) {
                                int bi = nb[rnd(static_cast<int>(nb.size()))];
                                Card* b = g.slots[bi];
                                g.slots[ai] = b;
                                g.slots[bi] = a;
                                a->origin = {a->x, a->y}; a->dest = {b->x, b->y}; a->prog = 0; a->inc = 0.13f; a->mixed = true; a->st = Card::St::Mix;
                                b->origin = {b->x, b->y}; b->dest = {a->x, a->y}; b->prog = 0; b->inc = 0.13f; b->mixed = false; b->st = Card::St::Mix;
                                app.sound().play_sound("card_switch", false, -1);
                            }
                        }
                    }
                }
            }
        } else if (c.st == Card::St::FaceDown) {
            c.flip->tick();
            if (c.flip->current_frame() == c.flip->total_frames() - 1) {
                c.st = Card::St::Idle;
                if (++g.counter == static_cast<int>(g.cards.size())) {  // FACEDOWN_DONE for all: start mixing
                    g.state = Grid::State::Mix;
                    g.timer = g.counter = 0;
                    g.to_mix.clear();
                    for (int rep = 0; rep < 2; ++rep)
                        for (auto& k : g.cards) g.to_mix.push_back(k.get());
                    // getMixPair
                    Card* a = g.to_mix[rnd(static_cast<int>(g.to_mix.size()))];
                    int ai = static_cast<int>(std::find(g.slots.begin(), g.slots.end(), a) - g.slots.begin());
                    std::vector<int> nb;
                    auto idle = [&](int i) { return g.slots[i]->st == Card::St::Idle; };
                    if ((ai + 1) % g.cols != 0 && idle(ai + 1)) nb.push_back(ai + 1);
                    if (ai % g.cols != 0 && idle(ai - 1)) nb.push_back(ai - 1);
                    if (ai >= g.cols && idle(ai - g.cols)) nb.push_back(ai - g.cols);
                    if (ai < static_cast<int>(g.cards.size()) - g.cols && idle(ai + g.cols)) nb.push_back(ai + g.cols);
                    if (!nb.empty()) {
                        int bi = nb[rnd(static_cast<int>(nb.size()))];
                        Card* b = g.slots[bi];
                        g.slots[ai] = b;
                        g.slots[bi] = a;
                        a->origin = {a->x, a->y}; a->dest = {b->x, b->y}; a->prog = 0; a->inc = 0.13f; a->mixed = true; a->st = Card::St::Mix;
                        b->origin = {b->x, b->y}; b->dest = {a->x, a->y}; b->prog = 0; b->inc = 0.13f; b->mixed = false; b->st = Card::St::Mix;
                        app.sound().play_sound("card_switch", false, -1);
                    }
                }
            }
        } else if (c.st == Card::St::Face) {
            if (c.flip->current_frame() > 0) c.flip->goto_and_stop(c.flip->current_frame());  // gotoAndStop(currentFrame - 1), frames are 1-based
            else { c.face_up = true; c.st = Card::St::Idle; }
        }
        if (c.hover && c.cross) c.cross->tick();
    }
    // --- grid state machine
    if (g.state == Grid::State::MoveIn) {
        if (g.timer++ > g.intro_interval) {
            g.timer = 0;
            app.sound().play_sound("card_deal", false, -1);
            Card& c = *g.cards[g.card_index];
            int total_w = g.cols * kCardW + g.cols * g.spacing, total_h = g.rows * kCardH + g.rows * g.spacing;
            float tx = static_cast<float>(g.spacing + g.cur_col * kCardW - total_w * 0.5f) + (g.cols - 2) * 5;
            float ty = static_cast<float>(g.spacing + g.cur_row * kCardH - total_h * 0.5f);
            c.origin = {c.x, c.y};
            c.dest = {static_cast<float>(static_cast<int>(tx)), static_cast<float>(static_cast<int>(ty))};
            c.prog = 0;
            c.inc = 0.06f;
            c.st = Card::St::MoveIn;
            ++g.card_index;
            if (++g.cur_col >= g.cols) { g.cur_col = 0; ++g.cur_row; }
            if (g.card_index == static_cast<int>(g.cards.size())) { g.card_index = 0; g.state = Grid::State::Idle; }
        }
    } else if (g.state == Grid::State::FaceUp) {
        if (g.timer++ > g.flip_interval) {
            app.sound().play_sound("card_flip", false, -1);
            g.timer = 0;
            g.flip_interval = 2;
            Card& c = *g.slots[g.card_index];
            c.face_up = false;
            if (c.flip) c.flip->goto_and_play(1);
            c.st = Card::St::FaceDown;
            if (--g.card_index == -1) g.state = Grid::State::Idle;  // mixing starts when the last flip lands (FACEDOWN_DONE x n)
        }
    }
}

void MinigameScreen::tick(App& app) {
    if (phase_ == Phase::Done) { finish(app); return; }
    MovieClip* mc = panel_.clip();
    mc->tick();
    if (dialogue_) dialogue_->tick();
    for (auto* h : {head_l_.get(), head_r_.get()}) {
        if (!h) continue;
        h->tick();
        if (auto* hs = h->child("cHeads"))
            if (auto* hd = hs->child("cHead"))
                if (hd->frame_of_label("talk") >= 0 && hd->current_frame() >= hd->frame_of_label("talk")) hd->stop();
    }
    if (grid_) tick_grid(app);
    int frame = mc->current_frame() + 1;
    switch (phase_) {
        case Phase::Entry:
            if (frame == 1) { if (MovieClip* b = mc->child("cMiniBacks")) b->goto_and_stop(game_ + 1); }
            else if (frame == 8 && !dialogue_) {
                const engine::Library* lib = mc->child("cDialogueClip") ? app.assets().library("minigame") : nullptr;
                if (lib) {
                    dialogue_ = MovieClip::create(lib, lib->symbol_id("Minigame_dialogue"), &app);
                    mc->child("cDialogueClip")->attach_child_clip(dialogue_.get());
                    show_line(0);
                }
            } else if (frame == 9 && !has_enough_coins() && dialogue_) {
                dialogue_->set_text("cText", app.tr("minigame.poor", "You don't have enough coins to play Bubble Buddy's card game."));
                mc->set_child_visible("cYES_cNO_SET", false);
            }
            if (mc->current_label() == "dialogue" && ++timer_ == 5) {
                timer_ = 0;
                if (has_enough_coins()) { init_yes_no(true); disabled_ = false; phase_ = Phase::Choice; }
                else show_too_poor();
            }
            break;
        case Phase::Choice:
            break;
        case Phase::LoadCards:
            if (timer_ == -1000) {  // playToMinigame
                if (mc->current_label() != "minigame") break;
                timer_ = 0;
            }
            if (++timer_ <= 2) break;
            if (!mc->child("cGRIDPLACEHOLDER")) break;
            load_cards();
            break;
        case Phase::Playing:
            break;
        case Phase::CardEarned:
            if (++timer_ >= 31) {
                if (has_enough_coins()) {
                    timer_ = 0;
                    grid_.reset();
                    mc->goto_label_and_stop("playAgain");
                    phase_ = Phase::PlayAgain;
                } else {
                    grid_.reset();
                    mc->play();
                    phase_ = Phase::ExitForwards;
                }
            }
            break;
        case Phase::PlayAgain:
            if (++timer_ >= 15) { init_yes_no(true); disabled_ = false; phase_ = Phase::Choice; }
            break;
        case Phase::ExitForwards:
            if (mc->current_label() == "end") phase_ = Phase::Done;
            break;
        case Phase::ExitBackwards:
            mc->goto_and_stop(mc->current_frame());  // gotoAndStop(currentFrame - 1)
            if (mc->current_frame() == 0) phase_ = Phase::Done;
            break;
        case Phase::TooPoorForwards:
            if (++timer_ < 70) break;
            if (timer_ == 70) mc->play();
            if (mc->current_frame() + 1 == mc->total_frames()) phase_ = Phase::Done;
            break;
        case Phase::TooPoorBackwards:
            if (++timer_ < 70) break;
            mc->goto_and_stop(mc->current_frame());
            if (mc->current_frame() == 0) phase_ = Phase::Done;
            break;
        case Phase::Done:
            break;
    }
    if (phase_ == Phase::Done) finish(app);
}

void MinigameScreen::pointer_move(App& app, Point p) {
    pointer_ = p;
    if (buttons_on_ && !disabled_) panel_.pointer_move(app, p);
    if (grid_ && !disabled_) {
        Point origin{sbso::kDesignW / 2.0f, sbso::kDesignH / 2.0f - 54.5f};
        for (auto& cp : grid_->cards) {
            Card& c = *cp;
            Rect r{origin.x + c.x, origin.y + c.y, origin.x + c.x + kCardW, origin.y + c.y + kCardH};
            bool over = c.mouse && p.x >= r.x0 && p.x < r.x1 && p.y >= r.y0 && p.y < r.y1;
            if (over && !c.hover) { app.sound().play_sound("battle_card_rollover", false, -1); c.hover = true; }
            else if (!over && c.hover) c.hover = false;
        }
    }
}

void MinigameScreen::pointer_down(App& app, Point p) {
    pointer_ = p;
    if (buttons_on_ && !disabled_) panel_.pointer_down(app, p);
}

void MinigameScreen::pointer_up(App& app, Point p) {
    pointer_ = p;
    if (disabled_) return;
    if (buttons_on_) {
        std::string b = panel_.pointer_up(app, p);
        if (b == "cYES") { yes_clicked(); return; }
        if (b == "cNO") { no_clicked(); return; }
    }
    if (grid_) {
        Point origin{sbso::kDesignW / 2.0f, sbso::kDesignH / 2.0f - 54.5f};
        for (size_t i = 0; i < grid_->slots.size(); ++i) {
            Card& c = *grid_->slots[i];
            Rect r{origin.x + c.x, origin.y + c.y, origin.x + c.x + kCardW, origin.y + c.y + kCardH};
            if (c.mouse && p.x >= r.x0 && p.x < r.x1 && p.y >= r.y0 && p.y < r.y1) { pick(static_cast<int>(i)); return; }
        }
    }
}

void MinigameScreen::draw(App&, std::vector<engine::DrawCmd>& out) {
    if (!panel_.valid()) return;
    panel_.draw(out);
    if (head_l_) head_l_->collect(out, panel_.place(), engine::ColorTransform{});
    if (head_r_) head_r_->collect(out, panel_.place(), engine::ColorTransform{});
    if (grid_) {
        const engine::Library* card_lib = app_.assets().library("cardAssets");
        static const float kWhite[4] = {1, 1, 1, 1};
        Matrix origin = panel_.place() * Matrix{1, 0, 0, 1, 0, -54.5f};
        for (auto& cp : grid_->cards) {
            Card& c = *cp;
            if (c.x <= -319) continue;  // not dealt yet
            float by = c.hover ? -5.0f : 0.0f;
            Matrix cm = origin * Matrix{1, 0, 0, 1, c.x, c.y};
            if (c.face_up) {
                engine::DrawCmd d;
                if (engine::image_cmd(card_lib, c.image, cm * Matrix{1, 0, 0, 1, 0, by}, &d)) out.push_back(d);
                if (!c.whammy) {
                    out.push_back(engine::text_cmd("unibody8black", 8, kWhite, c.cost, cm, 18, 72 + by));
                    out.push_back(engine::text_cmd("unibody8black", 8, kWhite, c.power, cm, 43, 72 + by));
                }
            }
            if (c.flip) c.flip->collect(out, cm, engine::ColorTransform{});
            if (c.hover && c.cross) c.cross->collect(out, cm * Matrix{1, 0, 0, 1, 0, 3 + by}, engine::ColorTransform{});
        }
    }
}

}  // namespace sbso::app
