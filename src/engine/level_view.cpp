#include "level_view.h"

#include <cstdio>
#include <cstdlib>

#include "soft_renderer.h"

#include <algorithm>

namespace sbso::engine {

bool LevelView::load(const std::string& root, const std::string& stage_file, int tileset, std::string* err) {
    root_ = root;
    baked_.reset();
    baked_for_ = nullptr;
    if (!tiles_.load(root + "/characters/tiles__" + stage_file + ".json", root + "/images/tiles/" + stage_file, err)) return false;
    if (!units_.load(root + "/characters/units.json", root + "/images/units", err)) return false;
    tile_char_ = tiles_.symbol_id("tiles.T" + std::to_string(tileset));
    if (tile_char_ < 0) { if (err) *err = "no tiles.T" + std::to_string(tileset) + " in " + stage_file; return false; }
    return true;
}

void LevelView::tile_cmds(int tile, std::vector<DrawCmd>& out, float x, float y) {
    auto it = tile_cache_.find(tile);
    if (it == tile_cache_.end()) {
        auto clip = MovieClip::create(&tiles_, tile_char_, &host_);
        clip->goto_and_stop(tile);
        std::vector<DrawCmd> cmds;
        clip->collect(cmds, Matrix{}, ColorTransform{});
        it = tile_cache_.emplace(tile, std::move(cmds)).first;
    }
    for (DrawCmd c : it->second) {
        c.m.tx += x;
        c.m.ty += y;
        out.push_back(c);
    }
}

MovieClip* LevelView::unit_clip(int id, const std::string& type) {
    auto it = unit_clips_.find(id);
    if (it != unit_clips_.end()) return it->second.get();
    const Library* lib = &units_;
    int cls = -1;
    if (type == "map_flag" && flag_lib_) {
        lib = flag_lib_;
        cls = lib->symbol_id("tourbus.CoreFlag");
    } else {
        cls = units_.symbol_id("units." + type);
        if (cls < 0) cls = units_.symbol_id("units.NOTFOUND");
    }
    if (cls < 0) return nullptr;
    if (std::getenv("SBSO_DEBUG_UNITS")) std::fprintf(stderr, "unit clip: id=%d type=%s symbol=%d\n", id, type.c_str(), cls);
    auto clip = MovieClip::create(lib, cls, &host_);
    if (type == "map_flag") clip->goto_label_and_stop("LOCKED");
    else clip->goto_label_and_stop("ready");
    MovieClip* p = clip.get();
    unit_clips_[id] = std::move(clip);
    return p;
}

void LevelView::build_tiles(const sbso::game::Level& level, std::vector<DrawCmd>& out, bool bake) {
    if (bake) {
        if (!baked_ || baked_for_ != &level) {
            std::vector<DrawCmd> cmds;
            build_tiles(level, cmds, false);
            int w = level.width * kTile, h = level.height * kTile;
            SoftRenderer r(w, h);
            r.set_images_root(root_ + "/images");
            r.clear(0, 0, 0, 0);
            r.draw(cmds);
            baked_ = std::make_shared<RawBitmap>();
            baked_->w = w;
            baked_->h = h;
            baked_->rgba = r.rgba8();
            baked_for_ = &level;
        }
        DrawCmd d;
        d.kind = DrawCmd::Kind::Raw;
        d.raw = baked_;
        d.smooth = true;
        out.push_back(std::move(d));
        return;
    }
    for (int y = 0; y < level.height; ++y)
        for (int x = 0; x < level.width; ++x) {
            int idx = x + y * level.width;
            if (idx < static_cast<int>(level.tiles.size())) tile_cmds(level.tiles[idx], out, static_cast<float>(x * kTile), static_cast<float>(y * kTile));
        }
}

void LevelView::tick_units() {
    for (auto& [id, c] : unit_clips_) c->tick();
}

void LevelView::build_units(const std::vector<UnitVisual>& units, std::vector<DrawCmd>& out) {
    std::vector<const UnitVisual*> order;
    for (const auto& u : units)
        if (u.visible) order.push_back(&u);
    std::stable_sort(order.begin(), order.end(), [](const UnitVisual* a, const UnitVisual* b) { return a->y < b->y; });  // Map.sortUnits
    for (const UnitVisual* u : order) {
        MovieClip* c = unit_clip(u->id, u->type);
        if (!c) continue;
        auto it = unit_labels_.find(u->id);
        if (it == unit_labels_.end() || it->second != u->label) {
            if (!c->goto_label_and_stop(u->label) && u->type != "map_flag") c->goto_label_and_stop("ready");
            unit_labels_[u->id] = u->label;
        }
        c->collect(out, Matrix{1, 0, 0, 1, u->x, u->y}, ColorTransform{});
    }
}

void LevelView::build(const sbso::game::Level& level, const sbso::game::Sim* sim, std::vector<DrawCmd>& out) {
    build_tiles(level, out);
    if (!sim) return;
    std::vector<UnitVisual> vis;
    for (int i = 0; i < sim->unit_count(); ++i) {
        const auto& u = sim->unit(i);
        UnitVisual v;
        v.id = i;
        v.type = u.type;
        v.x = static_cast<float>(u.x * kTile + kTile / 2);
        v.y = static_cast<float>(u.y * kTile + kTile / 2);
        v.visible = u.alive;
        vis.push_back(v);
    }
    build_units(vis, out);
}

}  // namespace sbso::engine
