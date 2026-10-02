#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <optional>
#include <string>

#include "game/sim.h"

using namespace sbso::game;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static const std::string kMaps = MAPS_DIR;

static SimSetup default_setup(int stage, int level, int barrels) {
    SimSetup s;
    s.stage = stage; s.level = level; s.barrel_prob = barrels;
    s.hand[0] = {"MOVEMENT_SPRINT_1", "MOVEMENT_WALK_1"};
    s.hand[1] = {"ATTACK_BUBBLE_PUNCH_1", "ATTACK_BAD_BREATH_1"};
    s.hand[2] = {"DEFEND_PILLOW_1"};
    s.hand[3] = {"NICK_FIRST_AID_2", "ATTACK_ANCHOR_TOSS_1"};
    return s;
}

// FNV-1a over the externally visible event stream, used to compare runs.
static void mix(std::uint64_t& h, std::uint64_t v) { h = (h ^ v) * 1099511628211ULL; }

static void hash_events(Sim& sim, std::uint64_t& h) {
    for (const Event& e : sim.take_events()) {
        mix(h, static_cast<std::uint64_t>(e.kind)); mix(h, e.a + 7); mix(h, e.b + 7); mix(h, e.c + 7); mix(h, e.d + 7);
        for (const Pt& p : e.path) { mix(h, p.x); mix(h, p.y); }
        for (char c : e.text) mix(h, static_cast<unsigned char>(c));
    }
}

static int dist_to_enemies(const Sim& sim, Pt p) {
    int d = 1 << 29;
    for (int i = 1; i < sim.unit_count(); ++i) {
        const SimUnit& u = sim.unit(i);
        if (u.alive && u.attackable) d = std::min(d, std::abs(u.x - p.x) + std::abs(u.y - p.y));
    }
    return d;
}

// Very simple bot: attack anything in range, otherwise approach the nearest enemy, then end the turn.
static void bot_turn(Sim& sim, const Database& db) {
    for (int pass = 0; pass < 6 && sim.can_act(); ++pass) {
        bool acted = false;
        for (CardType t : {CardType::Attack, CardType::Nick}) {
            for (const HandCard& c : sim.hand(t)) {
                if (!sim.card_available(c.name) || db.card(c.name)->effect == "HEAL") continue;
                for (const Pt& p : sim.range_for(c.name)) {
                    if (sim.board().unit_at(p.x, p.y) != kNoUnit && sim.play_attack(c.name, p)) { acted = true; break; }
                }
                if (acted || sim.finished()) break;
            }
            if (acted) break;
        }
        if (acted) continue;
        std::string bc; int bestd = 1 << 30;
        for (const HandCard& c : sim.hand(CardType::Move)) {
            if (!sim.card_available(c.name)) continue;
            for (const Pt& p : sim.range_for(c.name)) {
                int d = dist_to_enemies(sim, p);
                if (d < bestd) { bestd = d; bc = c.name; }
            }
        }
        if (bc.empty()) break;
        Pt dest{}; int dd = 1 << 30;
        for (const Pt& p : sim.range_for(bc)) {
            int d = dist_to_enemies(sim, p);
            if (d < dd) { dd = d; dest = p; }
        }
        if (!sim.play_move(bc, dest)) break;
    }
    if (sim.awaiting_gauge()) sim.provide_gauge(10);
    if (sim.finished()) return;
    sim.end_turn();
    if (sim.awaiting_gauge()) sim.provide_gauge(10);
}

static void check_invariants(const Sim& sim) {
    for (int i = 0; i < sim.unit_count(); ++i) {
        const SimUnit& u = sim.unit(i);
        if (!u.alive) continue;
        CHECK(u.health >= 0 && u.health <= u.max_health && u.energy >= 0 && u.energy <= u.max_energy);
        if (u.type != "barrel") CHECK(sim.board().unit_at(u.x, u.y) == i);
    }
}

static std::uint64_t play(const Database& db, const Level& lvl, const SimSetup& setup, std::uint64_t seed, int max_turns, bool* won, bool* lost, int* turns_used) {
    Sim sim(db, lvl, setup, seed);
    std::uint64_t h = 1469598103934665603ULL;
    int turn = 0;
    for (; turn < max_turns && !sim.finished(); ++turn) {
        hash_events(sim, h);
        CHECK(sim.turn() == Turn::Player);
        check_invariants(sim);
        bot_turn(sim, db);
    }
    hash_events(sim, h);
    *won = sim.won(); *lost = sim.lost(); *turns_used = turn;
    return h;
}

static Level open_level() {
    Level l; l.name = "test"; l.width = 9; l.height = 5; l.tileset = 1;
    l.collision.assign(45, 1); l.tiles.assign(45, 1);
    l.sb_x = 1; l.sb_y = 2; l.flag_x = 8; l.flag_y = 0;
    return l;
}

static bool has_event(const std::vector<Event>& v, Event::Kind k) {
    return std::any_of(v.begin(), v.end(), [&](const Event& e) { return e.kind == k; });
}

static void test_directed(const Database& db) {
    Level l = open_level();
    Spawn sp; sp.name = "eel_blue"; sp.x = 2; sp.y = 2;  // right next to SpongeBob
    l.spawns.push_back(sp);
    SimSetup setup = default_setup(5, 1, 0);
    Sim sim(db, l, setup, 99);
    sim.take_events();
    CHECK(sim.turn() == Turn::Player && sim.can_act());
    const Card* punch = db.card("ATTACK_BUBBLE_PUNCH_1");
    int enemy_hp = sim.unit(1).health, sb_energy = sim.unit(0).energy;
    CHECK(sim.range_for("ATTACK_BUBBLE_PUNCH_1").size() >= 1);
    CHECK(!sim.play_attack("ATTACK_BUBBLE_PUNCH_1", {5, 2}));  // nothing there / out of range
    CHECK(sim.play_attack("ATTACK_BUBBLE_PUNCH_1", {2, 2}));
    CHECK(sim.unit(0).energy == sb_energy - punch->cost);
    CHECK(sim.unit(1).health == std::max(0, enemy_hp - punch->damage));
    CHECK(!sim.card_available("ATTACK_BUBBLE_PUNCH_1"));       // a card can be used once per turn
    auto ev = sim.take_events();
    CHECK(has_event(ev, Event::Kind::Battle));
    // Shield: equipping costs energy and is consumed by the next hit.
    int e0 = sim.unit(0).energy;
    if (sim.card_available("DEFEND_PILLOW_1")) {
        CHECK(sim.play_defend("DEFEND_PILLOW_1"));
        CHECK(sim.shields().size() == 1 && sim.unit(0).energy < e0);
    }
    sim.end_turn();
    sim.take_events();
    CHECK(sim.turn() == Turn::Player || sim.finished());
    CHECK(sim.unit(0).energy == sim.unit(0).max_energy);       // everyone refills at turn start

    // Movement: out-of-range destinations are rejected; reaching a tile next to the flag wins.
    Level l2 = open_level();
    l2.sb_x = 6; l2.sb_y = 0; l2.flag_x = 8; l2.flag_y = 0;
    Sim s2(db, l2, default_setup(5, 1, 0), 5);
    CHECK(!s2.play_move("MOVEMENT_WALK_1", {0, 4}));
    CHECK(s2.play_move("MOVEMENT_WALK_1", {7, 0}));
    CHECK(s2.won());
    CHECK(has_event(s2.take_events(), Event::Kind::LevelWon));
}

static void test_snapshot(const Database& db) {
    int compared = 0;
    const char* stage_names[] = {"bikini_bottom", "kelp_forest", "ship_graveyard", "atlantis_park", "cliffs", "suburbs", "palace", "caves", "planktons_den"};
    for (int st : {1, 3, 6, 9}) {
        for (int lv : {1, 3}) {
            Level lvl; std::string err;
            if (!load_level(kMaps + "/" + stage_names[st - 1] + "_" + std::to_string(lv) + ".xml", &lvl, &err)) continue;
            SimSetup setup = default_setup(st, lv, 5);
            setup.belt_plus_health = 900;  // survive long enough to exercise mid-level snapshots
            Sim a(db, lvl, setup, 4242 + st * 7 + lv);
            std::uint64_t ha = 1469598103934665603ULL, hb = ha;
            for (int t = 0; t < 4 && !a.finished(); ++t) { hash_events(a, ha); bot_turn(a, db); }
            hash_events(a, ha);
            if (a.finished()) continue;
            CHECK(a.can_snapshot());
            std::vector<std::uint8_t> bytes = a.snapshot();
            std::optional<Sim> b;
            CHECK(Sim::restore(db, lvl, bytes, &b));
            if (!b) continue;
            CHECK(b->snapshot() == bytes);  // restore -> snapshot is the identity
            hash_events(*b, hb); hb = ha = 0;  // events of the restored sim start empty
            for (int t = 0; t < 8 && !a.finished() && !b->finished(); ++t) {
                hash_events(a, ha); hash_events(*b, hb);
                CHECK(ha == hb);
                bot_turn(a, db); bot_turn(*b, db);
                check_invariants(*b);
            }
            hash_events(a, ha); hash_events(*b, hb);
            CHECK(ha == hb);
            CHECK(a.snapshot() == b->snapshot());
            // Corruption and mismatches are rejected.
            std::optional<Sim> bad;
            std::vector<std::uint8_t> cut(bytes.begin(), bytes.begin() + bytes.size() / 2);
            CHECK(!Sim::restore(db, lvl, cut, &bad));
            std::vector<std::uint8_t> flipped = bytes; flipped[0] ^= 0xFF;
            CHECK(!Sim::restore(db, lvl, flipped, &bad));
            Level other = lvl; other.collision[0] ^= 1;
            CHECK(!Sim::restore(db, other, bytes, &bad));
            ++compared;
        }
    }
    std::printf("snapshot comparisons: %d\n", compared);
    CHECK(compared >= 5);
}

int main() {
    if (!std::filesystem::exists(MAPS_DIR)) { std::printf("skipped: no game files at %s (see game/README.md)\n", MAPS_DIR); return 0; }
    Database db; std::string err;
    if (!db.load(kMaps, &err)) { std::printf("%s\n", err.c_str()); return 1; }
    const char* stage_names[] = {"bikini_bottom", "kelp_forest", "ship_graveyard", "atlantis_park", "cliffs", "suburbs", "palace", "caves", "planktons_den"};
    test_directed(db);
    test_snapshot(db);
    int runs = 0, wins = 0, losses = 0;
    for (int st = 1; st <= 9; ++st) {
        for (int lv = 1; lv <= 6; ++lv) {
            Level lvl; std::string e;
            std::string path = kMaps + "/" + stage_names[st - 1] + "_" + std::to_string(lv) + ".xml";
            if (!load_level(path, &lvl, &e)) continue;
            SimSetup setup = default_setup(st, lv, st == 3 || st == 6 || st == 8 || st == 9 ? 5 : 0);
            bool w1, l1, w2, l2; int t1, t2;
            auto h1 = play(db, lvl, setup, 1234 + st * 10 + lv, 60, &w1, &l1, &t1);
            auto h2 = play(db, lvl, setup, 1234 + st * 10 + lv, 60, &w2, &l2, &t2);
            CHECK(h1 == h2);  // determinism: same seed and inputs give the same event stream
            ++runs; wins += w1; losses += l1;
        }
    }
    std::printf("runs=%d wins=%d losses=%d\n", runs, wins, losses);
    CHECK(runs == 54);
    std::printf(failures ? "%d FAILED\n" : "all sim tests passed\n", failures);
    return failures ? 1 : 0;
}
