#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

#include "game/sim.h"
#include "save/progression.h"
#include "save/store.h"

using namespace sbso::save;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static const std::string kMaps = MAPS_DIR;

static std::vector<DefaultCard> defaults() {
    return {{"MOVE", "MOVEMENT_WALK_1", true}, {"MOVE", "MOVEMENT_SPRINT_1", true}, {"MOVE", "MOVEMENT_CRAWL_1", false},
            {"ATTACK", "ATTACK_BUBBLE_PUNCH_1", true}, {"DEFEND", "DEFEND_PILLOW_1", true}, {"NICK", "NICK_FIRST_AID_2", true}};
}

static void flip_byte(const fs::path& p, size_t pos) {
    std::fstream f(p, std::ios::in | std::ios::out | std::ios::binary);
    f.seekg(pos); char c; f.get(c); c ^= 0x55; f.seekp(pos); f.put(c);
}

static void test_primitives() {
    const char* s = "123456789";
    CHECK(crc32(reinterpret_cast<const std::uint8_t*>(s), 9) == 0xCBF43926u);
    for (size_t n = 0; n < 12; ++n) {
        std::vector<std::uint8_t> v(n), back;
        for (size_t i = 0; i < n; ++i) v[i] = static_cast<std::uint8_t>(i * 37 + 5);
        CHECK(base64_decode(base64_encode(v), &back) && back == v);
    }
    std::vector<std::uint8_t> junk;
    CHECK(!base64_decode("abc", &junk) && !base64_decode("ab!d", &junk));
    CHECK(SaveStore::valid_name("Bob") && SaveStore::valid_name("Señor Cangrejo") && !SaveStore::valid_name("") && !SaveStore::valid_name("1234567890123456"));
    CHECK(!SaveStore::valid_id("../x") && SaveStore::valid_id("p0001"));
}

static void test_progress() {
    Progress p = new_progress("Patricio", defaults());
    CHECK(p.coins == 3 && p.gained_belts[0] == 1 && p.unlocked_stages[0][0] == kUnlocked && p.unlocked_stages[0][1] == kLocked);
    CHECK(p.belted[0].size() == 2 && p.all_cards[0].size() == 3);
    add_coins(p, 5000); CHECK(p.coins == 999);
    add_coins(p, -5000); CHECK(p.coins == 0);
    add_new_card(p, 1, "ATTACK_X"); CHECK(p.all_cards[1].front() == "ATTACK_X" && p.new_cards[1].size() == 1);
}

static void test_progression() {
    Progress p = new_progress("X", defaults());
    CHECK(next_highest_level(p, 0) == 0 && next_highest_level(p, 1) == -1);
    // Stage 1: flag 1 unlocks flag 2 and 3 + the minigame; clearing flag 1 first unlocks flag 2.
    LevelComplete c = begin_level_complete(p, 1, 1);
    CHECK(!c.direct && !c.final_flag && c.unlock == std::vector<int>{1} && !c.minigame);
    CHECK(finish_unlocks(p, 1, 1, c) == 1 && p.unlocked_stages[0][0] == kCleared && p.unlocked_stages[0][1] == kUnlocked);
    c = begin_level_complete(p, 1, 2);
    CHECK(c.unlock == (std::vector<int>{2, 3}) && c.minigame);
    CHECK(finish_unlocks(p, 1, 2, c) == 3 && p.unlocked_stages[0][2] == kUnlocked && p.unlocked_stages[0][3] == kUnlocked && p.unlocked_bonuses[0] == kCleared);
    // Flag 3 (index 2) unlocks nothing: the bus goes to the next highest unlocked level on the main path (3 -> index 3).
    c = begin_level_complete(p, 1, 3);
    CHECK(c.direct && c.next == 3 && p.unlocked_stages[0][2] == kCleared);
    // Even stages have no bonus state.
    set_bonus_state(p, 2, 1, kCleared); CHECK(p.unlocked_bonuses[1] == kLocked);
    // Finishing a stage unlocks the next one.
    for (int l = 0; l < 5; ++l) p.unlocked_stages[0][l] = kCleared;
    p.unlocked_stages[0][5] = kUnlocked;
    c = begin_level_complete(p, 1, 6);
    CHECK(c.final_flag && p.unlocked_stages[0][5] == kCleared && next_highest_level(p, 0) == -1);
    unlock_next_stage(p, 1); CHECK(p.unlocked_stages[1][0] == kUnlocked);
    CHECK(latest_level(p) == std::make_pair(2, 1));
    CHECK(latest_level(new_progress("Y", defaults())) == std::make_pair(1, 1));
    CHECK(!all_stages_complete(p));
    p.unlocked_stages[8][5] = kCleared; CHECK(all_stages_complete(p));
    for (int s = 1; s <= 9; ++s) CHECK(main_path(s).front() == 0 && main_path(s).back() == 5);
}

int main() {
    if (!std::filesystem::exists(MAPS_DIR)) { std::printf("skipped: no game files at %s (see game/README.md)\n", MAPS_DIR); return 0; }
    test_primitives();
    test_progress();
    test_progression();

    fs::path root = fs::path(TEST_TMP_DIR) / "save_test";
    fs::remove_all(root);
    SaveStore store(root);
    std::string err;

    // Settings.
    Settings st; CHECK(store.load_settings(&st, &err) && st.language.empty() && st.graphics_filter == "lanczos");
    st.language = "es-ES"; st.music_vol = 0.25; st.graphics_filter = "original"; st.fullscreen = false;
    CHECK(store.save_settings(st, &err));
    Settings st2; CHECK(store.load_settings(&st2, &err) && st2.language == "es-ES" && st2.music_vol == 0.25 && st2.graphics_filter == "original" && !st2.fullscreen);

    // Profiles.
    ProfileInfo a, b;
    CHECK(store.create_profile("Bob Esponja", defaults(), &a, &err));
    CHECK(store.create_profile("Patricio", defaults(), &b, &err));
    CHECK(!store.create_profile("", defaults(), &b, &err));
    CHECK(!store.create_profile("un nombre demasiado largo", defaults(), &b, &err));
    auto profs = store.list_profiles();
    CHECK(profs.size() == 2 && profs[0].name == "Bob Esponja" && profs[1].id != profs[0].id);
    CHECK(store.rename_profile(a.id, "Bob", &err) && store.list_profiles()[0].name == "Bob");
    for (int i = 0; i < kMaxProfiles; ++i) { ProfileInfo x; store.create_profile("P" + std::to_string(i), defaults(), &x, &err); }
    CHECK(store.list_profiles().size() == static_cast<size_t>(kMaxProfiles));
    CHECK(!store.create_profile("extra", defaults(), &b, &err));

    // Slots: autosave exists after creation, manual slots start empty.
    auto slots = store.list_slots(a.id);
    CHECK(slots.size() == static_cast<size_t>(kManualSlots + 1) && slots[0].exists && !slots[1].exists);

    // Round trip with a real mid-level snapshot.
    sbso::game::Database db; CHECK(db.load(kMaps, &err));
    sbso::game::Level lvl; CHECK(sbso::game::load_level(kMaps + "/bikini_bottom_2.xml", &lvl, &err));
    sbso::game::SimSetup setup; setup.stage = 1; setup.level = 2;
    setup.hand[0] = {"MOVEMENT_WALK_1"}; setup.hand[1] = {"ATTACK_BUBBLE_PUNCH_1"};
    sbso::game::Sim sim(db, lvl, setup, 7);
    sim.take_events();
    CHECK(sim.can_snapshot());
    Slot s;
    s.progress = new_progress("Bob", defaults());
    add_coins(s.progress, 40);
    s.progress.completed_missions[2] = true;
    s.progress.killed_kelps.push_back({3, 4, 5});
    s.progress.unlocked_stages[0][1] = kCleared;
    s.progress.encountered_enemies = {"eel_blue", "kelp"};
    s.snapshot = SnapshotBlob{1, 2, "2026-10-01T12:00:00Z", sim.snapshot()};
    CHECK(store.save_slot(a.id, 3, s, &err));
    Slot r;
    CHECK(store.load_slot(a.id, 3, &r, &err));
    CHECK(r.progress.coins == 43 && r.progress.completed_missions[2] && r.progress.killed_kelps.size() == 1 && r.progress.killed_kelps[0].y == 5);
    CHECK(r.progress.unlocked_stages[0][1] == kCleared && r.progress.encountered_enemies.size() == 2);
    CHECK(r.snapshot && r.snapshot->data == s.snapshot->data && r.snapshot->stage == 1 && r.snapshot->level == 2);
    std::optional<sbso::game::Sim> restored;
    CHECK(sbso::game::Sim::restore(db, lvl, r.snapshot->data, &restored) && restored && restored->snapshot() == s.snapshot->data);
    slots = store.list_slots(a.id);
    CHECK(slots[3].exists && slots[3].has_snapshot && slots[3].modified_unix > 0);

    // Atomic overwrite keeps a backup; a damaged main file falls back to it.
    Slot s2 = s; s2.progress.coins = 500; s2.snapshot.reset();
    CHECK(store.save_slot(a.id, 3, s2, &err));
    CHECK(fs::exists(root / "profiles" / a.id / "slot3.json.bak"));
    fs::path main = root / "profiles" / a.id / "slot3.json";
    flip_byte(main, 120);
    Slot rb; CHECK(store.load_slot(a.id, 3, &rb, &err) && rb.progress.coins == 43 && rb.snapshot);  // recovered the previous version
    flip_byte(root / "profiles" / a.id / "slot3.json.bak", 120);
    CHECK(!store.load_slot(a.id, 3, &rb, &err));
    CHECK(store.list_slots(a.id)[3].corrupt);

    // Copy / delete.
    CHECK(store.copy_slot(a.id, 0, 5, &err) && store.list_slots(a.id)[5].exists);
    CHECK(store.delete_slot(a.id, 5, &err) && !store.list_slots(a.id)[5].exists);
    CHECK(!store.load_slot("../etc", 0, &rb, &err) && !store.load_slot(a.id, 99, &rb, &err));
    CHECK(store.set_last_slot(a.id, 3, &err) && store.list_profiles()[0].last_slot == 3);
    CHECK(store.delete_profile(b.id, &err) && store.list_profiles().size() == static_cast<size_t>(kMaxProfiles - 1));

    fs::remove_all(root);
    std::printf(failures ? "%d FAILED\n" : "all save tests passed\n", failures);
    return failures ? 1 : 0;
}
