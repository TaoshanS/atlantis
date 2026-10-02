// Persistent data model. Replaces the original single Flash SharedObject ("SBSO2Profiles")
// with: global Settings, a list of Profiles (players), and per-profile save Slots. A slot holds
// the progress state of the original profile object plus an optional mid-level snapshot.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sbso::save {

constexpr int kMaxProfiles = 12;      // the original allowed 3; limit still open (see docs/DECISIONS.md)
constexpr int kManualSlots = 8;       // slots 1..kManualSlots are manual; slot 0 is the autosave
constexpr int kNumStages = 9, kLevelsPerStage = 6, kNumBelts = 10, kNumCardTypes = 4;
constexpr int kMaxProfileName = 15;

enum LevelState { kLocked = 0, kUnlocked = 1, kCleared = 2 };

struct KilledUnit { int level = 0, x = 0, y = 0; };

// Everything in the original Profile object that is progress (not settings).
struct Progress {
    std::string name;
    int current_belt_index = 0;
    std::array<int, kNumBelts> gained_belts{};
    std::array<std::vector<std::string>, kNumCardTypes> belted, all_cards, new_cards;  // MOVE, ATTACK, DEFEND, NICK
    int coins = 3;  // clamped to 0..999
    std::array<std::array<int, kLevelsPerStage>, kNumStages> unlocked_stages{};
    std::array<int, kNumStages> unlocked_bonuses{};
    std::vector<std::string> encountered_enemies;
    std::array<bool, kNumStages> completed_missions{};
    std::vector<KilledUnit> killed_kelps, killed_doodles;
    bool first_run = true, seen_card_help = false, seen_belt_help = false, seen_flag_help = false, seen_coin_help = false;
    std::array<int, 4> minigame_plays{};
    int doodle_belt_index = 0;  // which doodlebob drops the belt (was re-rolled every session in the original)
};

struct SnapshotBlob {
    int stage = 0, level = 0;
    std::string created;           // ISO-8601, informational
    std::vector<std::uint8_t> data;  // Sim::snapshot()
};

struct Slot {
    Progress progress;
    std::optional<SnapshotBlob> snapshot;
    std::int64_t modified_unix = 0;
};

struct Settings {
    double music_vol = 0.6, fx_vol = 0.6;
    bool fullscreen = true;
    std::string language;                 // "", "en" or "es-ES" ("" = follow the system)
    std::string graphics_filter = "lanczos";  // "lanczos" or "original"
    std::string last_profile;                 // profile id used last session
    double map_fps = 24.0;                    // tick rate of the world map (button, flags, bus); the original ran the stage at 38. 0 = 38
};

struct ProfileInfo {
    std::string id;   // directory name, stable
    std::string name;
    int last_slot = 0;
};

struct SlotInfo {
    int slot = 0;
    bool exists = false, has_snapshot = false, corrupt = false;
    std::int64_t modified_unix = 0;
};

// Fresh progress, like Profile.createNewProfile(name): default cards come from SBSO2_CONFIG DefaultData.
struct DefaultCard { std::string type, name; bool in_belt = false; };
Progress new_progress(const std::string& name, const std::vector<DefaultCard>& defaults);

// Profile.addCoins semantics (clamped to 0..999).
void add_coins(Progress& p, int delta);
// Profile.addNewCard: records the card as new and prepends it to the collection.
void add_new_card(Progress& p, int card_type, const std::string& card);

}  // namespace sbso::save
