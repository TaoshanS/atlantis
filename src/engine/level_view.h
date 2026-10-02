// Draws a battle level (tile layer + units + flag) from the simulation state.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "game/level.h"
#include "game/sim.h"
#include "movieclip.h"

namespace sbso::engine {

constexpr int kTile = 30;  // Map.TILE_WIDTH / TILE_HEIGHT

class LevelView {
public:
    // `root` is the extractor output directory; `stage_file` is e.g. "cliffs" (tiles/<stage_file>.swf).
    bool load(const std::string& root, const std::string& stage_file, int tileset, std::string* err);

    // A unit as shown on screen (animation state, not necessarily the simulation's current state).
    struct UnitVisual {
        int id = -1;
        std::string type;
        float x = 0, y = 0;        // centre of the unit's tile in level pixels
        bool visible = true;
        std::string label = "ready";  // clip frame label ("ready", "move_left", "move_right", "win", ...)
    };

    // Appends commands for the whole level in level pixel space (0..width*30, 0..height*30).
    void build(const sbso::game::Level& level, const sbso::game::Sim* sim, std::vector<DrawCmd>& out);
    // Draws the tile layer. By default it is pre-composited once at 1x into a single bitmap (so scaling it later cannot
    // open seams between the overlapping translucent tile edges); bake=false emits individual tile quads.
    void build_tiles(const sbso::game::Level& level, std::vector<DrawCmd>& out, bool bake = true);
    void build_units(const std::vector<UnitVisual>& units, std::vector<DrawCmd>& out);
    void tick_units();  // advance the animation of every unit clip one frame
    MovieClip* unit_clip(int id, const std::string& type);
    const Library& tiles() const { return tiles_; }
    const Library* units_library() const { return &units_; }
    // The flag (a Unit in the original) is drawn from tourbus.swf's tourbus.CoreFlag clip.
    void set_flag_library(const Library* lib) { flag_lib_ = lib; }

private:
    void tile_cmds(int tile, std::vector<DrawCmd>& out, float x, float y);

    std::string root_;
    std::shared_ptr<RawBitmap> baked_;
    const sbso::game::Level* baked_for_ = nullptr;
    Library tiles_, units_;
    const Library* flag_lib_ = nullptr;
    int tile_char_ = -1;
    std::map<int, std::vector<DrawCmd>> tile_cache_;  // tile frame -> commands relative to the cell origin
    std::map<int, std::unique_ptr<MovieClip>> unit_clips_;
    std::map<int, std::string> unit_labels_;
    ClipHost host_;
};

}  // namespace sbso::engine
