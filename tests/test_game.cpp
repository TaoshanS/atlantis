#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <string>

#include "game/board.h"
#include "game/config.h"
#include "game/cutscene.h"
#include "game/database.h"
#include "game/dialogues.h"
#include "game/level.h"

using namespace sbso::game;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static const std::string kMaps = MAPS_DIR;

static void test_database() {
    Database db;
    std::string err;
    CHECK(db.load(kMaps, &err));
    if (!err.empty()) std::printf("%s\n", err.c_str());
    CHECK(db.cards().size() > 100);
    const Card* c = db.card("MOVEMENT_HOTFOOT_1");
    CHECK(c && c->type == CardType::Move && c->max == 7 && c->cost == 85);
    const EnemyDef& k = db.enemy("bot_mr_krabs");
    CHECK(k.health == 80 && k.energy == 100 && k.cards[static_cast<int>(CardType::Nick)].size() == 3 && k.cards[static_cast<int>(CardType::Attack)].empty());
    CHECK(k.drop_cards.size() == 3);
    CHECK(db.enemy("does_not_exist").name == "eel_blue");
    CHECK(!is_boss("bot_plankton_red") && !is_boss("bot_plankton") && is_boss("bubble_dirty"));
    CHECK(has_boss_status("bot_plankton_red") && is_friend("patrick") && !is_friend("kelp"));
}

static void test_levels(const Database& db) {
    int levels = 0, with_path = 0, blocked_spawns = 0;
    const char* stages[] = {"cliffs", "bikini_bottom", "kelp_forest", "ship_graveyard", "suburbs", "caves", "planktons_den", "atlantis_park", "palace"};
    for (const char* s : stages) {
        for (int n = 1; n <= 6; ++n) {
            Level l;
            std::string err;
            std::string path = kMaps + "/" + s + "_" + std::to_string(n) + ".xml";
            if (!load_level(path, &l, &err)) { std::printf("%s\n", err.c_str()); ++failures; continue; }
            ++levels;
            Board b(l.width, l.height, l.collision);
            CHECK(static_cast<int>(l.tiles.size()) >= l.width * l.height);
            CHECK(b.tile_value(l.sb_x, l.sb_y) != 0);
            for (const Spawn& sp : l.spawns) {
                if (b.tile_value(sp.x, sp.y) == 0) ++blocked_spawns;  // data quirk: caves_5 barrel
                (void)db.enemy(sp.name);
            }
            // Path from SpongeBob to the flag: every step must be a legal move.
            auto path_pts = b.find_path({l.sb_x, l.sb_y}, {l.flag_x, l.flag_y});
            if (!path_pts.empty()) {
                ++with_path;
                int px = l.sb_x, py = l.sb_y;
                for (const Pt& p : path_pts) {
                    CHECK(std::abs(p.x - px) + std::abs(p.y - py) == 1);
                    CHECK(b.tile_cost(px, py, p.x, p.y) == 1);
                    px = p.x; py = p.y;
                }
                CHECK(px == l.flag_x && py == l.flag_y);
            }
        }
    }
    std::printf("levels loaded: %d, with SB->flag path: %d\n", levels, with_path);
    CHECK(blocked_spawns <= 1);
    CHECK(levels == 54);
    CHECK(with_path >= 40);
}

static void test_ranges() {
    // Open 5x5 board, every tile shares layer bit 1.
    Board b(5, 5, std::vector<int>(25, 1));
    auto r1 = b.move_range({2, 2}, 1);
    CHECK(r1.size() == 4);
    auto r2 = b.move_range({2, 2}, 2);
    CHECK(r2.size() == 12);  // diamond radius 2 minus the origin
    b.set_unit(2, 1, 7);     // a unit blocks the tile and the path through it
    auto r3 = b.move_range({2, 2}, 1);
    CHECK(r3.size() == 3);
    auto all = [](int) { return true; };
    auto a = b.attack_range({2, 2}, 2, 1, "", all);
    CHECK(a.size() == 12 - 4 + 0 || a.size() > 0);
    // min=0 range 1 includes the 4 neighbours (origin itself is excluded because |d|=0 <= min).
    auto a1 = b.attack_range({2, 2}, 1, 0, "", all);
    CHECK(a1.size() == 4);
    // Non-attackable units (friends) are not highlighted.
    auto friends = [](int) { return false; };
    CHECK(b.attack_range({2, 2}, 1, 0, "", friends).size() == 3);
    // Different layer bits do not connect.
    std::vector<int> col(25, 1);
    col[2 + 1 * 5] = 2;
    Board c(5, 5, col);
    CHECK(c.tile_cost(2, 2, 2, 1) == -1 && c.tile_cost(2, 2, 3, 2) == 1);
    CHECK(c.tile_cost(2, 2, -1, 0) == kInvalidTile);
    // Cliff attacks reach lower-or-equal values even without a shared bit.
    CHECK(c.attack_range({2, 2}, 1, 0, "cliffs", all).size() == 3);  // value 2 > origin value 1: unreachable
    col[2 + 2 * 5] = 4;                                              // origin higher than its neighbour (2)
    Board d(5, 5, col);
    CHECK(d.attack_range({2, 2}, 1, 0, "cliffs", all).size() == 4);
    CHECK(d.attack_range({2, 2}, 1, 0, "", all).size() == 0);       // no shared bits at all
}

static void test_config() {
    Config cfg;
    std::string err;
    CHECK(cfg.load(kMaps, &err));
    CHECK(cfg.stages.size() == 9 && cfg.stages[4].name == "cliffs" && cfg.stages[8].name == "planktons_den");
    CHECK(cfg.belts.size() == 10 && cfg.belts[0].name == "greenunderpants" && cfg.belts[3].plus_health == -25);
    CHECK(cfg.level_file(5, 2) == "cliffs_2.xml" && cfg.level_file(10, 1) == "palace_you_win.xml");
    int in_belt = 0;
    for (const auto& c : cfg.default_cards) in_belt += c.in_belt;
    CHECK(in_belt == 6 && cfg.default_cards.size() == 18);
}

static void test_cutscenes() {
    Config cfg;
    std::string err;
    CHECK(cfg.load(kMaps, &err));
    CHECK(cfg.minigames.size() == 4 && cfg.minigames[0].num_cards == 4 && cfg.minigames[0].cost == 3 && cfg.minigames[3].whammies == 4);
    CHECK(cfg.minigames[0].commons.size() == 9 && cfg.minigames[0].rares.size() == 3);
    std::vector<CutStep> steps;
    CHECK(load_cutscene(kMaps, "minigame1.xml", &steps, &err));
    CHECK(steps.size() == 4 && steps[0].type == "fadein" && steps[1].type == "choice" && steps[1].dialogue.size() == 14 && steps[1].shortcut.size() == 12);
    CHECK(steps[2].type == "minigame" && steps[3].type == "fadeout");
    CHECK(steps[1].dialogue[0].key == "xml:minigame1.xml:speaker.text[0]" && steps[1].shortcut[0].key == "xml:minigame1.xml:speaker.text[14]");
    CHECK(load_cutscene(kMaps, "game_complete.xml", &steps, &err));
    bool has_move = false;
    for (const auto& st : steps)
        if (st.type == "move" && st.actor == "bubble" && st.from_x == 20 && st.to_x == 8) has_move = true;
    CHECK(has_move);
}

static void test_dialogues() {
    Dialogues d;
    std::string err;
    CHECK(d.load(kMaps, &err));
    int total = 0;
    for (auto& [k, v] : d.stage_intro) total += static_cast<int>(v.size());
    for (auto& [k, v] : d.boss) total += static_cast<int>(v.size());
    for (auto& [k, v] : d.boss_end) total += static_cast<int>(v.size());
    for (auto& v : d.belt) total += static_cast<int>(v.size());
    for (auto& [k, v] : d.card) total += static_cast<int>(v.size());
    for (auto& v : d.minigame) total += static_cast<int>(v.size());
    total += static_cast<int>(d.krusty_krab.size());
    CHECK(total > 200 && d.belt.size() == 10 && d.card.count("card_1") == 1);
    CHECK(!d.krusty_krab.empty() && d.krusty_krab[0].key.rfind("xml:SBSO2_CONFIG.xml:speaker.text[", 0) == 0);
    std::printf("dialogue lines: %d\n", total);
}

int main() {
    if (!std::filesystem::exists(MAPS_DIR)) { std::printf("skipped: no game files at %s (see game/README.md)\n", MAPS_DIR); return 0; }
    Database db;
    std::string err;
    if (!db.load(kMaps, &err)) { std::printf("%s\n", err.c_str()); return 1; }
    test_config();
    test_dialogues();
    test_cutscenes();
    test_database();
    test_levels(db);
    test_ranges();
    std::printf(failures ? "%d FAILED\n" : "all game tests passed\n", failures);
    return failures ? 1 : 0;
}
