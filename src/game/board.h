// Grid queries ported from Map.as / tinymantis/AStar.as. Pure data, no rendering.
//
// Ordering notes (fidelity): the original iterates AS3 Objects with for-in in a few places
// (move-range highlight creation, A* open list). We use insertion order, which is the order
// Ruffle gives; AI tie-breaking depends on it, so it is isolated here.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace sbso::game {

struct Pt {
    int x = 0, y = 0;
    bool operator==(const Pt& o) const { return x == o.x && y == o.y; }
};

constexpr int kInvalidTile = -1;

class Board {
public:
    Board() = default;
    Board(int w, int h, std::vector<int> collision);

    int width() const { return w_; }
    int height() const { return h_; }
    int tile_value(int x, int y) const;                 // Map.getTileValue
    int tile_cost(int x0, int y0, int x1, int y1) const;  // Map.getTile: 1 or kInvalidTile

    // Unit occupancy (unit ids, -1 = empty).
    int unit_at(int x, int y) const;
    bool has_object(int x, int y) const { return unit_at(x, y) != -1; }
    void set_unit(int x, int y, int id);

    // Map.showMoveRange: tiles reachable with `budget` steps, in discovery order (origin excluded).
    std::vector<Pt> move_range(Pt origin, int budget) const;

    // Map.showRange for attacks. `attackable(unit_id)` tells whether a unit may be targeted;
    // tiles are returned in creation order (rows top to bottom, columns left to right).
    std::vector<Pt> attack_range(Pt origin, int max, int min, const std::string& effect,
                                 const std::function<bool(int)>& attackable) const;

    // tinymantis/AStar.findPath: greedy best-first (cost = tile cost + 10*manhattan to goal).
    // Returns the path from the first step to the goal, or empty if none.
    std::vector<Pt> find_path(Pt from, Pt to, bool check_objects = false) const;

private:
    int idx(int x, int y) const { return x + y * w_; }
    bool in_bounds(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }

    int w_ = 0, h_ = 0;
    std::vector<int> collision_;
    std::vector<int> units_;
};

}  // namespace sbso::game
