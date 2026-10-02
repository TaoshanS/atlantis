// Renders a battle level (tiles + units from a freshly built Sim) to a PNG with the software renderer.
//   sbso_shot_level <extracted-dir> <game-maps-dir> <stage 1-9> <level 1-6> <out.png> [scale]
#include <cstdio>
#include <cstdlib>
#include <string>

#include "engine/level_view.h"
#include "engine/soft_renderer.h"
#include "engine/text.h"

using namespace sbso::engine;
using namespace sbso::game;

int main(int argc, char** argv) {
    if (argc < 6) { std::puts("usage: sbso_shot_level <extracted> <maps-dir> <stage> <level> <out.png> [scale]"); return 2; }
    static const char* stages[] = {"bikini_bottom", "kelp_forest", "ship_graveyard", "atlantis_park", "cliffs", "suburbs", "palace", "caves", "planktons_den"};
    std::string root = argv[1], maps = argv[2];
    int st = std::atoi(argv[3]), lv = std::atoi(argv[4]);
    float scale = argc > 6 ? static_cast<float>(std::atof(argv[6])) : 1.0f;
    Database db; std::string err;
    if (!db.load(maps, &err)) { std::puts(err.c_str()); return 1; }
    Level level;
    if (!load_level(maps + "/" + stages[st - 1] + "_" + std::to_string(lv) + ".xml", &level, &err)) { std::puts(err.c_str()); return 1; }
    SimSetup setup; setup.stage = st; setup.level = lv;
    setup.hand[0] = {"MOVEMENT_WALK_1"}; setup.hand[1] = {"ATTACK_BUBBLE_PUNCH_1"};
    Sim sim(db, level, setup, 1);
    LevelView view;
    if (!view.load(root, stages[st - 1], level.tileset, &err)) { std::puts(err.c_str()); return 1; }
    std::vector<DrawCmd> cmds;
    view.build(level, &sim, cmds);
    int w = static_cast<int>(level.width * kTile * scale), h = static_cast<int>(level.height * kTile * scale);
    SoftRenderer r(w, h);
    r.set_images_root(root + "/images");
    r.clear(0.1f, 0.1f, 0.1f, 1);
    // apply the view scale to every command
    for (DrawCmd& c : cmds) { c.m = Matrix{scale, 0, 0, scale, 0, 0} * c.m; }
    r.draw(cmds);
    std::printf("%s level %d: %dx%d tiles, %d units, %zu cmds\n", stages[st - 1], lv, level.width, level.height, sim.unit_count(), cmds.size());
    return r.save_png(argv[5]) ? 0 : 1;
}
