#include "board.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_map>

namespace sbso::game {

Board::Board(int w, int h, std::vector<int> collision) : w_(w), h_(h), collision_(std::move(collision)) {
    collision_.resize(static_cast<size_t>(w) * h, 0);
    units_.assign(static_cast<size_t>(w) * h, -1);
}

int Board::tile_value(int x, int y) const {
    int i = idx(x, y);
    return (i >= 0 && i < static_cast<int>(collision_.size())) ? collision_[i] : 0;
}

int Board::tile_cost(int x0, int y0, int x1, int y1) const {
    if (x1 < 0 || y1 < 0 || x1 >= w_ || y1 >= h_) return kInvalidTile;
    int target = collision_[idx(x1, y1)];
    int origin = tile_value(x0, y0);
    return (origin & target) != 0 ? 1 : -1;
}

int Board::unit_at(int x, int y) const {
    int i = idx(x, y);
    // Like the original array lookup, anything outside the grid reads as "no object".
    if (x < 0 || y < 0 || x >= w_ || y >= h_ || i < 0 || i >= static_cast<int>(units_.size())) return -1;
    return units_[i];
}

void Board::set_unit(int x, int y, int id) {
    if (!in_bounds(x, y)) return;
    units_[idx(x, y)] = id;
}

std::vector<Pt> Board::move_range(Pt origin, int budget) const {
    // dist[] keeps insertion order like the AS3 Object used by getExpendedTiles.
    std::vector<Pt> order;
    std::unordered_map<int, int> dist;
    auto key = [&](Pt p) { return p.x * 100000 + p.y; };
    order.push_back(origin);
    dist[key(origin)] = 0;
    std::vector<Pt> frontier{origin};
    static const Pt kDirs[4] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
    while (!frontier.empty()) {
        std::vector<Pt> next;
        for (const Pt& cur : frontier) {
            int d = dist[key(cur)];
            int left = budget - d;
            if (left <= 0) continue;
            for (const Pt& dir : kDirs) {
                Pt n{cur.x + dir.x, cur.y + dir.y};
                if (has_object(n.x, n.y)) continue;
                int cost = tile_cost(cur.x, cur.y, n.x, n.y);
                if (cost == kInvalidTile || cost > left) continue;
                cost += d;
                auto it = dist.find(key(n));
                if (it == dist.end()) {
                    dist[key(n)] = cost;
                    order.push_back(n);
                    next.push_back(n);
                } else if (cost < it->second) {
                    it->second = cost;
                    next.push_back(n);  // original pushes a fresh PointInt, so it is re-expanded
                }
            }
        }
        frontier = std::move(next);
    }
    order.erase(order.begin());  // delete the origin entry
    return order;
}

std::vector<Pt> Board::attack_range(Pt o, int max, int min, const std::string& effect,
                                    const std::function<bool(int)>& attackable) const {
    std::vector<Pt> out;
    int origin_value = tile_value(o.x, o.y);
    for (int dy = -max; dy <= max; ++dy) {
        int lo = dy < 0 ? -(max + dy) : -(max - dy);
        int hi = dy < 0 ? (max + dy) : (max - dy);
        int ady = std::abs(dy);
        for (int dx = lo; dx <= hi; ++dx) {
            if (ady + std::abs(dx) <= min) continue;
            int tx = o.x + dx, ty = o.y + dy;
            if (!in_bounds(tx, ty)) continue;
            int v = collision_[idx(tx, ty)];
            if (v == 0) continue;
            int uid = units_[idx(tx, ty)];
            if (uid != -1 && !attackable(uid)) continue;
            bool linked = (v & origin_value) != 0;
            if (effect == "cliffs") linked = linked || v <= origin_value;
            if (linked) out.push_back({tx, ty});
        }
    }
    return out;
}

namespace {
struct Node {
    int x, y, parent = 0;
    double cost = 0;
    int id() const { return (x << 16) | y; }
};
}  // namespace

std::vector<Pt> Board::find_path(Pt from, Pt to, bool check_objects) const {
    std::vector<Pt> path;
    if (tile_value(to.x, to.y) == 0) return path;
    if (check_objects && (has_object(from.x, from.y) || has_object(to.x, to.y))) return path;

    std::vector<Node> open;                    // insertion-ordered "Object"
    std::unordered_map<int, Node> closed;
    Node cur{from.x, from.y};
    closed[cur.id()] = cur;
    bool found = false;
    static const int kDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (;;) {
        if (!open.empty()) {
            double best = 100000;
            size_t bi = 0;
            for (size_t i = 0; i < open.size(); ++i) {
                if (open[i].cost < best) { best = open[i].cost; bi = i; }
            }
            cur = open[bi];
            closed[cur.id()] = cur;
            open.erase(open.begin() + bi);
        }
        for (const auto& d : kDirs) {
            int nx = cur.x + d[0], ny = cur.y + d[1];
            int tc = kInvalidTile;
            if (!has_object(nx, ny) || (nx == to.x && ny == to.y)) tc = tile_cost(cur.x, cur.y, nx, ny);
            if (tc == kInvalidTile || tc >= 1000) continue;
            Node n{nx, ny, cur.id(), 0};
            if (nx == to.x && ny == to.y) { cur = n; found = true; break; }
            if (closed.count(n.id())) continue;
            n.cost = tc + (std::abs(nx - to.x) + std::abs(ny - to.y)) * 10;
            auto it = std::find_if(open.begin(), open.end(), [&](const Node& o) { return o.id() == n.id(); });
            if (it == open.end()) open.push_back(n);
            else if (n.cost < it->cost) *it = n;
        }
        if (found || open.empty()) break;
    }
    if (!found) return path;
    Node n = cur;
    while (!(n.x == from.x && n.y == from.y)) {
        path.push_back({n.x, n.y});
        n = closed.at(n.parent);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

}  // namespace sbso::game
