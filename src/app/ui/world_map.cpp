#include "world_map.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace sbso::app {
using engine::MovieClip;
using engine::Point;

namespace {

int pint(const std::string& s) { return std::atoi(s.c_str()); }
std::string pad2(int n) { return n < 10 ? "0" + std::to_string(n) : std::to_string(n); }
engine::Matrix at(Point p) { return engine::Matrix{1, 0, 0, 1, p.x, p.y}; }

// MapScreenData.sortNodes on long names ("P_F08_01"): by flag number, then length, then sub index.
bool node_less(const std::string& a, const std::string& b) {
    int na = pint(a.substr(3, 2)), nb = pint(b.substr(3, 2));
    if (na != nb) return na < nb;
    if (a.size() != b.size()) return a.size() < b.size();
    return pint(a.size() > 6 ? a.substr(6, 2) : "") < pint(b.size() > 6 ? b.substr(6, 2) : "");
}

}  // namespace

// ------------------------------------------------------------------------------------------------- MapPaths

MapPaths::MapPaths(const std::vector<MovieClip::NamedChild>& children) {
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        const std::string& n = it->name;
        if (n.find("P_") == std::string::npos || n.size() < 4) continue;
        std::string sh = n.substr(3);
        long2short_[n] = sh;
        if (n.find("_P") == std::string::npos && n.find("_J") == std::string::npos) {
            interest_.push_back(n);
            if (n.find("_E") != std::string::npos) end_ = n;
        }
        nodes_[sh] = it->pos;
    }
    std::sort(interest_.begin(), interest_.end(), node_less);
    for (size_t i = 0; i + 1 < interest_.size(); ++i)
        for (size_t j = i + 1; j < interest_.size(); ++j) {
            const std::string& a = long2short_[interest_[i]];
            const std::string& b = long2short_[interest_[j]];
            auto p = find_path(a, b);
            paths_[a + "," + b] = p;
            std::reverse(p.begin(), p.end());
            paths_[b + "," + a] = p;
        }
}

void MapPaths::range2(const std::string& a, const std::string& b, std::vector<Point>& out) const {
    for (int n = pint(a.substr(0, 2)); n <= pint(b.substr(0, 2)); ++n) {
        auto it = nodes_.find(pad2(n));
        if (it != nodes_.end()) out.push_back(it->second);
    }
}

void MapPaths::range5(const std::string& a, const std::string& b, bool forward, std::vector<Point>& out) const {
    std::string prefix = a.substr(0, 3);
    int from = pint(a.substr(3, 2)), to = pint(b.substr(3, 2));
    for (int n = from; forward ? n <= to : n >= to; n += forward ? 1 : -1) {
        auto it = nodes_.find(prefix + pad2(n));
        if (it != nodes_.end()) out.push_back(it->second);
    }
}

std::vector<Point> MapPaths::find_path(const std::string& a, const std::string& b) const {
    std::vector<Point> out;
    if (a.size() == 2 && b.size() == 2) {
        range2(a, b, out);
    } else if (a.size() == 2 && b.size() == 5) {
        range2(a, b.substr(0, 2), out);
        range5(b.substr(0, 3) + "01", b, true, out);
    } else if (a.size() == 5 && b.size() == 2) {
        range5(a, a.substr(0, 3) + "01", false, out);
        range2(a.substr(0, 2), b, out);
    } else if (a.size() == 5 && b.size() == 5) {
        if (a.substr(0, 2) != b.substr(0, 2)) {
            range5(a, a.substr(0, 3) + "01", false, out);
            range2(a.substr(0, 2), b.substr(0, 2), out);
            range5(b.substr(0, 3) + "01", b, true, out);
        } else {
            range5(a, b, true, out);
        }
    }
    return out;
}

std::vector<Point> MapPaths::path(const std::string& from, const std::string& to) const {
    auto fa = long2short_.find(from), fb = long2short_.find(to);
    if (fa == long2short_.end() || fb == long2short_.end()) return {};
    auto it = paths_.find(fa->second + "," + fb->second);
    return it == paths_.end() ? std::vector<Point>{} : it->second;
}

// ------------------------------------------------------------------------------------------------- TourBus

TourBus::TourBus(App& app, Point pos) : app_(app), pos_(pos) {
    if (const engine::Library* lib = app.assets().library("main")) {
        clip_owned_ = MovieClip::create(lib, lib->symbol_id("SpongeBobTourBus_TOUR_BUS"), &app);
        if (clip_owned_) clip_owned_->goto_label_and_stop("right");
        clip_ = clip_owned_.get();
    }
    state_ = current_ = State::Idle;
}

void TourBus::stop_sound() {
    if (sound_ >= 0) app_.sound().stop_sound(sound_);
    sound_ = -1;
}

void TourBus::move_init(std::vector<Point> path) {
    if (state_ == State::Move) {
        bool found = false;
        for (size_t i = 1; i < path_.size() && !found; ++i) {
            for (int j = static_cast<int>(path.size()) - 1; j >= 0; --j) {
                if (path_[i].x == path[j].x && path_[i].y == path[j].y) {
                    path_.resize(i);
                    path.erase(path.begin(), path.begin() + j);
                    found = true;
                    break;
                }
            }
        }
        path_.insert(path_.end(), path.begin(), path.end());
    } else {
        path_ = std::move(path);
        set_state(State::Move);
        stop_sound();  // a drive that ended this very frame may not have been closed yet
        sound_ = app_.sound().play_sound("mapscreen_ship_movement_loop", true, -1);
    }
}

void TourBus::enter_move() {
    if (path_.size() < 2) { set_state(State::Idle); return; }
    pos_ = path_[0];
    Point d{path_[1].x - path_[0].x, path_[1].y - path_[0].y};
    double len = std::sqrt(static_cast<double>(d.x) * d.x + static_cast<double>(d.y) * d.y);
    inc_ = len > 8 ? 8 / len : 1;
    prog_ = 0;
    if (!clip_) return;
    if (d.x * d.x > d.y * d.y) clip_->goto_label_and_stop(d.x < 0 ? "left" : "right");
    else clip_->goto_label_and_stop(d.y < 0 ? "up" : "down");
}

void TourBus::tick() {
    if (pending_) {
        pending_ = false;
        if (current_ == State::Move) {  // MOVE_EXIT
            stop_sound();
            if (on_terminal) { auto cb = on_terminal; cb(); }
            else if (on_done) { auto cb = on_done; cb(); }
        }
        current_ = state_;
        if (current_ == State::Move) enter_move();
        else if (clip_) clip_->goto_label_and_stop("right");
    }
    if (current_ != State::Move || path_.size() < 2) return;
    prog_ += inc_;
    auto interp = [](float a, float b, double t) { double k = (1 - std::cos(t * 3.14159265358979323846)) / 2; return static_cast<float>(a * (1 - k) + b * k); };
    if (prog_ < 1) {
        pos_.x = interp(path_[0].x, path_[1].x, prog_);
        pos_.y = interp(path_[0].y, path_[1].y, prog_);
    } else {
        pos_ = path_[1];
        path_.erase(path_.begin());
        if (path_.size() > 1) enter_move();
        else set_state(State::Idle);
    }
}

// ------------------------------------------------------------------------------------------------- MapFlag

MapFlag* MapFlag::selected = nullptr;

void MapFlag::unselect() {
    if (!selected) return;
    if (MovieClip* f = selected->flag()) f->goto_label_and_stop("offDone");
    selected->add_listeners();
    selected = nullptr;
}

MapFlag::MapFlag(App& app, Kind kind, Point pos, int id, std::string name) : app_(app), pos_(pos), id_(id), name_(std::move(name)) {
    static const char* const kSymbols[5] = {"flagMarker_LOCKED", "flagMarker_UNLOCKED", "flagMarker_CLEARED", "whiteFlag_LOCKED", "whiteFlag_CLEARED"};
    locked_ = kind == Kind::Locked || kind == Kind::MinigameLocked;
    if (const engine::Library* lib = app.assets().library("flagMarkers")) clip_owned_ = MovieClip::create(lib, lib->symbol_id(kSymbols[static_cast<int>(kind)]), &app);
    clip_ = clip_owned_.get();
}

bool MapFlag::hit(Point p) const {
    engine::Rect r;
    if (!clip_ || !clip_->child_bounds("hitter", &r)) return false;
    float x = p.x - pos_.x, y = p.y - pos_.y;
    return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1;
}

void MapFlag::enable() {
    enabled_ = true;
    if (MovieClip* f = flag()) f->goto_label_and_play("off");
    watch_off_ = true;
    add_listeners();
}

void MapFlag::select() {
    selected = this;
    if (MovieClip* f = flag()) f->goto_label_and_stop("selected");
    remove_listeners();
}

void MapFlag::roll_over() {
    if (!enabled_) return;
    watch_off_ = false;
    app_.sound().play_sound(locked_ ? "mapscreen_rollover_locked" : "mapscreen_rollover_unlocked", false, -1);
    if (MovieClip* f = flag()) f->goto_label_and_play("over");
    watch_over_ = true;
}

void MapFlag::roll_out() {
    if (!enabled_) return;
    watch_over_ = false;
    if (MovieClip* f = flag()) f->goto_label_and_play("off");
    watch_off_ = true;
}

void MapFlag::clicked() {
    if (!enabled_) return;
    app_.sound().play_sound("mapscreen_click_unlocked_level", false, -1);
    if (selected == this) return;
    watch_off_ = watch_over_ = false;
    if (MovieClip* f = flag()) f->goto_label_and_stop("selected");
    watch_select_ = true;
    if (selected) {
        selected->watch_off_ = selected->watch_over_ = false;
        selected->enabled_ = true;
        if (MovieClip* f = selected->flag()) f->goto_label_and_stop("offDone");
        selected->add_listeners();
    }
    selected = this;
    remove_listeners();
    if (on_clicked) on_clicked(this);
}

void MapFlag::pointer_move(Point p) {
    if (!listening_) { hover_ = false; return; }
    bool h = hit(p);
    if (h && !hover_) { hover_ = true; roll_over(); }
    else if (!h && hover_) { hover_ = false; roll_out(); }
}

bool MapFlag::pointer_click(Point p) {
    if (!listening_ || locked_ || !hit(p)) return false;
    clicked();
    return true;
}

void MapFlag::recycle() {
    if (selected == this) selected = nullptr;  // FlagMarker.recycle: sSelected = null
    else selected = nullptr;
    watch_off_ = watch_over_ = watch_select_ = false;
    remove_listeners();
}

void MapFlag::tick() {
    MovieClip* f = flag();
    if (!f) return;
    if (!started_) {  // FlagMarker.onComplete
        started_ = true;
        if (!init_select_ && selected != this) {
            enable();
        } else {
            selected = this;
            f->goto_label_and_stop("selected");
            watch_select_ = true;
        }
    }
    if (watch_off_ && f->current_label() == "offDone") { f->stop(); watch_off_ = false; }
    if (watch_over_ && f->current_label() == "overDone") { f->stop(); watch_over_ = false; }
    if (watch_select_) {
        if (f->current_label() != "selected") f->goto_label_and_stop("selected");
        MovieClip* lo = f->child("cCrosshairsLow");
        MovieClip* hi = f->child("cCrosshairsHigh");
        if (lo && hi) {
            lo->goto_and_play(1);
            hi->goto_and_play(1);
            watch_select_ = false;
        }
    }
}

// ------------------------------------------------------------------------------------------------- UnlockFlag

UnlockFlag::UnlockFlag(App& app, bool minigame, Point pos, int id) : pos_(pos), id_(id) {
    if (const engine::Library* lib = app.assets().library("flagMarkers")) clip_owned_ = MovieClip::create(lib, lib->symbol_id(minigame ? "whiteFlag_UNLOCKING" : "flagMarker_UNLOCKING"), &app);
    clip_ = clip_owned_.get();
    app.sound().play_sound("mapscreen_level_unlock", false, -1);
}

bool UnlockFlag::tick() {
    if (done_ || !clip_) return false;
    MovieClip* f = clip_->child("cFlag");
    if (!f) return false;
    if (f->current_frame() + 1 == f->total_frames()) {
        f->stop();
        done_ = true;
        return true;
    }
    // FlagMarker_unlock.playUnlock: the white (minigame) flag's cFlag is parked on frame 1 by a stop() script and has to be started
    if (f->current_frame() == 0 && !f->playing()) f->play();
    return false;
}

// ------------------------------------------------------------------------------------------------- StageMap

StageMap::StageMap(App& app, int stage, StageMapHost* host) : app_(app), host_(host), stage_(stage) {
    const engine::Library* lib = app.assets().library("LevelSelectMaps");
    if (lib) map_ = MovieClip::create(lib, lib->symbol_id("map_level" + std::to_string(stage)), &app);
    flags_.resize(7);
    if (!map_) return;
    paths_ = std::make_unique<MapPaths>(map_->named_children());
    build_flags();
    int idx = check_bus();
    if (idx != -1 && idx < 7 && flags_[idx].flag) flags_[idx].flag->set_select();
    else idx = 0;
    if (flags_[idx].flag || flags_[0].flag) current_flag_ = (flags_[idx].flag ? flags_[idx] : flags_[0]).name;
}

StageMap::~StageMap() = default;

void StageMap::build_flags() {
    save::Progress& pr = app_.progress();
    for (const auto& n : map_->named_children()) {
        int idx = -1;
        MapFlag::Kind kind;
        std::string name;
        if (n.name.find("FORT") != std::string::npos) {
            idx = n.name.size() > 4 ? n.name[4] - '1' : 0;
            if (idx < 0 || idx > 5) continue;
            int st = pr.unlocked_stages[stage_ - 1][idx];
            kind = st == save::kUnlocked ? MapFlag::Kind::Unlocked : st == save::kCleared ? MapFlag::Kind::Cleared : MapFlag::Kind::Locked;
            name = n.name;
            name.replace(name.find("FORT"), 4, "FLAG");
        } else if (n.name.find("MINIG") != std::string::npos) {
            idx = 6;
            kind = pr.unlocked_bonuses[stage_ - 1] == save::kCleared ? MapFlag::Kind::MinigameCleared : MapFlag::Kind::MinigameLocked;
            name = n.name;
            name.replace(name.find("MINIG"), 5, "MINNI");
        } else {
            continue;
        }
        Slot& s = flags_[idx];
        s.pos = n.pos;
        s.name = name;
        s.flag = std::make_unique<MapFlag>(app_, kind, n.pos, idx, name);
        s.flag->on_clicked = [this](MapFlag* f) { flag_clicked(f); };
        MovieClip* c = map_->add_child_clip(s.flag->take_clip(), at(n.pos));
        s.flag->bind(c);
    }
}

int StageMap::the_level() const {
    for (const Slot& s : flags_)
        if (s.flag && s.name == current_flag_) return s.flag->id() + 1;
        else if (s.unlock && s.name == current_flag_) return s.unlock->id() + 1;
    return 1;
}

void StageMap::place_bus_at_flag(int level) {
    if (!paths_ || level < 0 || level > 6 || flags_[level].name.empty()) return;
    std::string from = node_of(flags_[level].name);
    std::string to = level < 5 && !flags_[level + 1].name.empty() ? node_of(flags_[level + 1].name) : paths_->end_point();
    auto p = paths_->path(from, to);
    if (!p.empty()) bus_->set_pos(p[0]);
}

void StageMap::get_bus(int level) {
    bus_ = std::make_unique<TourBus>(app_, Point{0, 0});
    place_bus_at_flag(level);
    bus_->on_done = [this]() { host_->tour_bus_done(); };
    if (MovieClip* c = map_->add_child_clip(bus_->take_clip(), bus_->matrix())) bus_->bind(c);
    bus_in_map_ = true;
}

int StageMap::check_bus() {
    save::Progress& pr = app_.progress();
    Session& ss = app_.session();
    int r = -1;
    if (pr.unlocked_stages[stage_ - 1][0] != save::kLocked) {
        if (ss.level_just_completed || stage_ == ss.stage) r = ss.level - 1;
        else r = save::next_highest_level(pr, stage_ - 1);
        if (r == -1 || r > 6 || !flags_[r].flag) r = 0;
        if (!bus_) get_bus(r);
        else place_bus_at_flag(r);
    } else if (!bus_) {
        bus_ = std::make_unique<TourBus>(app_, flags_[0].pos);  // never added to the map (as in the original)
        bus_->on_done = [this]() { host_->tour_bus_done(); };
    }
    return r;
}

void StageMap::move_bus_to_flag(const std::string& name) {
    std::string from = node_of(current_flag_), to = node_of(name);
    current_flag_ = name;
    auto p = paths_->path(from, to);
    if (p.empty()) { host_->tour_bus_done(); return; }
    bus_->move_init(std::move(p));
}

void StageMap::move_bus_to_terminal(bool end) {
    MapFlag::unselect();
    std::string from = node_of(current_flag_);
    std::string to = end ? paths_->end_point() : std::string("P_S00");
    bus_->on_terminal = [this]() { bus_at_terminal(); };
    auto p = paths_->path(from, to);
    if (p.empty()) { bus_at_terminal(); return; }
    bus_->move_init(std::move(p));
}

void StageMap::bus_at_terminal() {
    place_bus_at_flag(0);
    bus_->on_terminal = nullptr;
    save::unlock_next_stage(app_.progress(), stage_);
    host_->stage_complete();
}

void StageMap::flag_clicked(MapFlag* f) {
    Session& ss = app_.session();
    if (ss.stage != stage_) ss.stage = stage_;
    if (!ss.level_just_completed) {
        ss.level = f->id() + 1;
        host_->flag_clicked();
        move_bus_to_flag(f->name());
    }
}

void StageMap::check_flag_and_bus_states() {
    for (Slot& s : flags_)
        if (s.flag) s.flag->enable();
    int idx = check_bus();
    if (idx != -1 && flags_[idx].flag) {
        flags_[idx].flag->select();
        current_flag_ = flags_[idx].name;
    }
}

void StageMap::replace_with_flag(int idx, MapFlag::Kind kind) {
    Slot& s = flags_[idx];
    auto nf = std::make_unique<MapFlag>(app_, kind, s.pos, idx, s.name);
    nf->on_clicked = [this](MapFlag* f) { flag_clicked(f); };
    MovieClip* old = s.flag ? s.flag->clip() : (s.unlock ? s.unlock->clip() : nullptr);
    if (s.flag) s.flag->recycle();
    MovieClip* c = map_->replace_child_clip(old, nf->take_clip(), at(s.pos));
    nf->bind(c);
    s.unlock.reset();
    s.flag = std::move(nf);
}

void StageMap::begin_unlock(int idx, bool minigame) {
    Slot& s = flags_[idx];
    if (!s.flag) return;
    auto uf = std::make_unique<UnlockFlag>(app_, minigame, s.pos, idx);
    MovieClip* old = s.flag->clip();
    s.flag->recycle();
    MovieClip* c = map_->replace_child_clip(old, uf->take_clip(), at(s.pos));
    uf->bind(c);
    s.flag.reset();
    s.unlock = std::move(uf);
}

void StageMap::unlock_done(int idx) {
    host_->unlock_ani_done();
    save::Progress& pr = app_.progress();
    Session& ss = app_.session();
    if (idx == 6) {
        replace_with_flag(6, MapFlag::Kind::MinigameCleared);
        save::set_bonus_state(pr, ss.stage, stage_ - 1, save::kCleared);
    } else {
        replace_with_flag(idx, MapFlag::Kind::Unlocked);
        pr.unlocked_stages[stage_ - 1][idx] = save::kUnlocked;
    }
    if (--unlock_counter_ == 0) {
        int dest = save::finish_unlocks(pr, stage_, ss.level, complete_);
        if (dest != -1 && flags_[dest].flag) {
            ss.level = dest + 1;
            move_bus_to_flag(flags_[dest].name);
            flags_[dest].flag->clicked();
        }
        ss.level_just_completed = false;
    }
}

void StageMap::show_level_complete_ani() {
    Session& ss = app_.session();
    int level0 = ss.level - 1;
    if (level0 < 0 || level0 > 5 || !flags_[level0].flag) { ss.level_just_completed = false; host_->tour_bus_done(); return; }
    replace_with_flag(level0, MapFlag::Kind::Cleared);
    complete_ = save::begin_level_complete(app_.progress(), stage_, ss.level);
    if (!complete_.final_flag) {
        if (complete_.direct) {
            int next = complete_.next;
            if (next >= 0 && flags_[next].flag) {
                ss.level = next + 1;
                move_bus_to_flag(flags_[next].name);
                flags_[next].flag->clicked();
                ss.level_just_completed = false;
            } else {
                move_bus_to_terminal(true);
                ss.level_just_completed = false;
            }
        } else {
            unlock_counter_ = static_cast<int>(complete_.unlock.size());
            for (int f : complete_.unlock) begin_unlock(f, false);
            if (complete_.minigame) {
                ++unlock_counter_;
                begin_unlock(6, true);
            }
        }
    } else {
        if (stage_ < 9) {
            app_.sound().play_sound("battle_level_victory", false, -1);
            move_bus_to_terminal(true);
            for (Slot& s : flags_)
                if (s.flag) s.flag->disable();
        }
        ss.level_just_completed = false;
        host_->check_complete_all_stages();
    }
}

void StageMap::tick() {
    for (int i = 0; i < 7; ++i) {
        Slot& s = flags_[i];
        if (s.flag) s.flag->tick();
        if (s.unlock && s.unlock->tick()) { unlock_done(i); }
    }
    if (bus_) {
        bus_->tick();
        if (bus_in_map_) map_->set_child_clip_matrix(bus_->clip(), bus_->matrix());
    }
}

void StageMap::pointer_move(Point local) {
    for (Slot& s : flags_)
        if (s.flag) s.flag->pointer_move(local);
}

void StageMap::pointer_click(Point local) {
    for (int i = 6; i >= 0; --i)
        if (flags_[i].flag && flags_[i].flag->pointer_click(local)) return;
}

void StageMap::recycle() {
    for (Slot& s : flags_)
        if (s.flag) s.flag->recycle();
}

}  // namespace sbso::app
