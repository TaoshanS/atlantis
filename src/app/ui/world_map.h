// The world map pieces of the level select: path graph (MapScreenData.as), the tour bus (SpongeBobTourBus.as), flags
// (FlagMarker.as / FlagMarker_unlock.as) and one stage map with its progression animations (LevelSelectScreenMap.as).
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/app.h"
#include "save/progression.h"

namespace sbso::app {

// Nodes P_xx on a map clip joined into drivable paths between the points of interest (flags, start, end).
class MapPaths {
public:
    explicit MapPaths(const std::vector<engine::MovieClip::NamedChild>& children);
    // Long node names ("P_F01"); empty when there is no path (same node).
    std::vector<engine::Point> path(const std::string& from, const std::string& to) const;
    const std::string& end_point() const { return end_; }

private:
    std::vector<engine::Point> find_path(const std::string& a, const std::string& b) const;
    void range2(const std::string& a, const std::string& b, std::vector<engine::Point>& out) const;
    void range5(const std::string& a, const std::string& b, bool forward, std::vector<engine::Point>& out) const;
    std::map<std::string, engine::Point> nodes_;  // short name ("06_01") -> position
    std::map<std::string, std::string> long2short_;
    std::vector<std::string> interest_;
    std::map<std::string, std::vector<engine::Point>> paths_;
    std::string end_;
};

class TourBus {
public:
    TourBus(App& app, engine::Point pos);
    std::unique_ptr<engine::MovieClip> take_clip() { return std::move(clip_owned_); }  // the map owns the clip afterwards
    void bind(engine::MovieClip* c) { clip_ = c; }
    engine::MovieClip* clip() const { return clip_; }
    void set_pos(engine::Point p) { pos_ = p; }
    engine::Point pos() const { return pos_; }
    ~TourBus() { stop_sound(); }
    void stop_sound();   // the engine loop must never outlive the drive (map switched, bus replaced)
    void move_init(std::vector<engine::Point> path);
    void tick();  // one ENTER_FRAME
    bool moving() const { return state_ == State::Move; }
    // Which event fires when the drive ends (BUS_AT_TERMINAL is only heard while a terminal drive is on).
    std::function<void()> on_done, on_terminal;
    engine::Matrix matrix() const { return engine::Matrix{1, 0, 0, 1, pos_.x, pos_.y}; }

private:
    enum class State { Idle, Move };
    void enter_move();
    void set_state(State s) { state_ = s; pending_ = true; }
    App& app_;
    std::unique_ptr<engine::MovieClip> clip_owned_;
    engine::MovieClip* clip_ = nullptr;
    engine::Point pos_;
    std::vector<engine::Point> path_;
    State state_ = State::Idle, current_ = State::Idle;
    bool pending_ = false;
    double prog_ = 0, inc_ = 0;
    int sound_ = -1;
};

class MapFlag {
public:
    enum class Kind { Locked, Unlocked, Cleared, MinigameLocked, MinigameCleared };
    MapFlag(App& app, Kind kind, engine::Point pos, int id, std::string name);
    std::unique_ptr<engine::MovieClip> take_clip() { return std::move(clip_owned_); }
    void bind(engine::MovieClip* c) { clip_ = c; }
    engine::MovieClip* clip() { return clip_; }
    static MapFlag* selected;  // FlagMarker.sSelected
    static void unselect();    // FlagMarker.unSelect

    void tick();
    void set_select() { init_select_ = true; }
    void select();
    void enable();
    void disable() { enabled_ = false; }
    void recycle();
    bool locked() const { return locked_; }
    // Pointer in the space of the map the flag lives in.
    void pointer_move(engine::Point p);
    bool pointer_click(engine::Point p);  // true when the flag was clicked
    void clicked();                       // also used by the map to auto-select a flag
    int id() const { return id_; }
    const std::string& name() const { return name_; }
    engine::Point pos() const { return pos_; }
    std::function<void(MapFlag*)> on_clicked;  // "flagClicked"

private:
    friend class StageMap;
    bool hit(engine::Point p) const;
    void add_listeners() { listening_ = true; }
    void remove_listeners() { listening_ = false; hover_ = false; }
    void roll_over();
    void roll_out();
    void watch_select() { watch_select_ = true; }
    engine::MovieClip* flag() { return clip_ ? clip_->child("cFlag") : nullptr; }
    App& app_;
    std::unique_ptr<engine::MovieClip> clip_owned_;
    engine::MovieClip* clip_ = nullptr;
    engine::Point pos_;
    int id_;
    std::string name_;
    bool locked_ = false, enabled_ = false, init_select_ = false, listening_ = false, hover_ = false, started_ = false;
    bool watch_off_ = false, watch_over_ = false, watch_select_ = false;
    int rollover_sound_ = -1;
};

// FlagMarker_unlock: the flag lighting up; done() once the animation reached its last frame.
class UnlockFlag {
public:
    UnlockFlag(App& app, bool minigame, engine::Point pos, int id);
    std::unique_ptr<engine::MovieClip> take_clip() { return std::move(clip_owned_); }
    void bind(engine::MovieClip* c) { clip_ = c; }
    bool tick();  // true on the frame the animation completes
    int id() const { return id_; }
    engine::Point pos() const { return pos_; }
    engine::MovieClip* clip() { return clip_; }

private:
    std::unique_ptr<engine::MovieClip> clip_owned_;
    engine::MovieClip* clip_ = nullptr;
    engine::Point pos_;
    int id_;
    bool done_ = false;
};

// Callbacks into the level select screen (the events LevelSelectScreenMap dispatches to its parent).
struct StageMapHost {
    virtual ~StageMapHost() = default;
    virtual void flag_clicked() = 0;              // enableScreen(false)
    virtual void tour_bus_done() = 0;             // enableScreen(true)
    virtual void stage_complete() = 0;
    virtual void check_complete_all_stages() = 0;
    virtual void unlock_ani_done() = 0;           // UNLOCK_ANI_DONE
};

class StageMap {
public:
    StageMap(App& app, int stage, StageMapHost* host);
    ~StageMap();
    engine::MovieClip* clip() { return map_.get(); }
    int stage() const { return stage_; }
    int the_level() const;                 // 1-based level of the flag the bus stands on
    bool current_flag_is_minigame() const { return current_flag_.find("MINNI") != std::string::npos; }
    void tick();
    void check_flag_and_bus_states();
    void show_level_complete_ani();
    void pointer_move(engine::Point local);
    void pointer_click(engine::Point local);  // press+release on the same spot
    void recycle();
    bool bus_moving() const { return bus_ && bus_->moving(); }
    void silence_bus() { if (bus_) bus_->stop_sound(); }

private:
    struct Slot {  // one of flags 0..5 / minigame flag 6: a MapFlag or, while lighting up, an UnlockFlag
        std::unique_ptr<MapFlag> flag;
        std::unique_ptr<UnlockFlag> unlock;
        engine::Point pos;
        std::string name;
    };
    void build_flags();
    int check_bus();
    void get_bus(int level);
    void place_bus_at_flag(int level);
    void move_bus_to_flag(const std::string& name);
    void move_bus_to_terminal(bool end);
    void flag_clicked(MapFlag* f);
    void replace_with_flag(int idx, MapFlag::Kind kind);
    void begin_unlock(int idx, bool minigame);
    void unlock_done(int idx);
    void bus_at_terminal();
    static std::string node_of(const std::string& flag_name) { return "P" + flag_name.substr(5); }
    const char* kind_name(int state) const;

    App& app_;
    StageMapHost* host_;
    int stage_;
    std::unique_ptr<engine::MovieClip> map_;
    std::unique_ptr<MapPaths> paths_;
    std::vector<Slot> flags_;  // 0..5 + 6 (minigame)
    std::unique_ptr<TourBus> bus_;
    bool bus_in_map_ = false;
    std::string current_flag_;
    save::LevelComplete complete_;
    int unlock_counter_ = 0;
};

}  // namespace sbso::app
