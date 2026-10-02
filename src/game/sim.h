// Deterministic battle simulation: a port of the gameplay logic spread over Unit, AIUnit,
// AIManager, TurnManager, BattleManager, GameLevel and Map in the original, with all
// rendering/animation removed. Everything here is plain data so it can be snapshotted;
// animation timing is replaced by an ordered event queue for the presentation layer.
#pragma once
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "board.h"
#include "core/rng.h"
#include "database.h"
#include "level.h"

namespace sbso::game {

enum class Turn { Player = 0, Computer = 1 };
enum class UState { Idle, Move, Think, Route, Attack, Battle, Win };

constexpr int kNoUnit = -1;

// Thrown where the original would raise a runtime error inside a frame handler (the handler
// aborts, the rest of the game keeps running).
struct AsError {};

struct SimUnit {
    int id = kNoUnit;
    std::string type;
    int x = 0, y = 0;
    int health = 0, max_health = 0, energy = 0, max_energy = 0;
    bool alive = true;
    bool is_ai = false;
    bool attackable = true;
    UState state = UState::Idle;
    std::optional<UState> next_state;  // StateMachine.setState is applied on the next tick
    std::optional<Pt> move_target;

    // AIUnit data
    std::array<std::vector<std::string>, kNumCardTypes> cards;
    std::vector<std::string> drop_cards;
    bool has_drop_cards = false;  // mDropCards != null
    int drop_empties = 0;
    int coins = 0;
    int scan_range = -1;  // -1: use the enemy definition
    bool belt = false;
    bool has_boss_attacks = false;
    std::vector<std::string> boss_attacks;
    bool has_instructions = false;  // _mInstructions != null
    std::vector<std::string> instructions;
    std::string curr_attack, curr_move;
    bool attacked_this_turn = false, run_from_sb = false, turn_done = true;
    std::vector<std::string> defense_cards;
};

struct HandCard {
    std::string name;
    bool used = false;
};

struct KilledKelp { int level = 0, x = 0, y = 0; };

struct MissionState {
    std::array<bool, 9> complete{};
    int kelp_remaining = 6;  // MiniMissionManager._mKelpNum
    int doodle_total = 20;
    int which_doodle = 1;    // 1-based index of the doodlebob that drops the belt
    int doodles_killed = 0;
    std::vector<KilledKelp> killed_kelps, killed_doodles;
};

struct SimSetup {
    int stage = 1, level = 1;
    int barrel_prob = 0;                 // GameInfo.getBarrelInfo (percent)
    int belt_plus_health = 0, belt_plus_energy = 0;
    std::array<std::vector<std::string>, kNumCardTypes> hand;       // belted cards per type
    std::array<std::vector<std::string>, kNumCardTypes> owned;      // Profile.mAllCards (drop de-duplication)
    MissionState missions;
};

struct Event {
    enum class Kind {
        TurnStarted,        // a = turn (0 player / 1 computer)
        UnitSpawned,        // a = unit
        UnitActive,         // a = enemy unit starting its turn (the original shows its HUD panel)
        UnitMoved,          // a = unit, path = steps
        UnitRemoved,        // a = unit
        EnergyChanged,      // a = unit
        HealthChanged,      // a = unit
        Battle,             // a = attacker, b = defender, text = attack card, text2 = defense card, c = damage, d = shield damage
        CounterGauge,       // a = attacker (SpongeBob), c = max damage
        CoinsGained,        // c = amount
        CardDropped,        // text = card
        BeltDropped,        // text = enemy type (belt cutscene)
        ShieldEquipped,     // text = card
        ShieldConsumed,     // text = card
        Healed,             // c = amount
        NickCardConsumed,   // text = card
        MissionCompleted,   // a = mission index
        BossDefeated,       // a = unit
        LevelWon,
        LevelLost,
    } kind;
    Event(Kind k) : kind(k) {}
    int a = -1, b = -1, c = 0, d = 0;
    int h0 = 0, e0 = 0;  // Battle: defender health and attacker energy before the exchange
    int f = 0;  // Battle: bit 1 defender defeated, 2 drops coins, 4 drops a card, 8 drops a belt, 16 barrel releases a jellyfish
    std::string text, text2;
    std::vector<Pt> path;
};

class Sim {
public:
    // Map.bubbleOffList: the scenery units (effect="exit") leave in a puff of bubbles (after the stage intro dialogue).
    void bubble_off_list();
    Sim(const Database& db, const Level& level, const SimSetup& setup, std::uint64_t seed);

    // ---- player API ----
    Turn turn() const { return turn_; }
    bool can_act() const;  // player's turn, no pending gauge, not finished
    bool card_available(const std::string& card) const;  // in hand, unused, affordable (and not a useless heal)
    std::vector<Pt> range_for(const std::string& card) const;
    bool play_move(const std::string& card, Pt dest);
    bool play_attack(const std::string& card, Pt target);
    bool play_defend(const std::string& card);
    bool play_heal(const std::string& card);
    void end_turn();
    // Counter-attack gauge (SpongeBob counters): power in [0, max]; the original rounds it.
    bool awaiting_gauge() const { return gauge_pending_; }
    void provide_gauge(double power);
    void gauge_timeout();

    // ---- state ----
    const SimUnit& unit(int id) const { return units_[id]; }
    int unit_count() const { return static_cast<int>(units_.size()); }
    int sb_id() const { return sb_; }
    const Board& board() const { return board_; }
    bool finished() const { return won_ || lost_; }
    bool won() const { return won_; }
    bool lost() const { return lost_; }
    const std::vector<HandCard>& hand(CardType t) const { return hand_[static_cast<int>(t)]; }
    const std::vector<std::string>& shields() const { return defense_cards_; }
    const MissionState& missions() const { return missions_; }
    std::vector<Event> take_events();
    Rng& rng() { return rng_; }

    // ---- snapshots (mid-level save) ----
    // Only valid at a turn boundary: the player's turn, nothing pending. The level is identified
    // by a hash of its collision data so a snapshot is never loaded onto a different map.
    bool can_snapshot() const { return can_act() && events_.empty(); }
    std::vector<std::uint8_t> snapshot() const;
    // Returns false (and leaves `out` untouched) if the data is truncated, corrupt, from another
    // format version or from a different level.
    static bool restore(const Database& db, const Level& level, const std::vector<std::uint8_t>& data, std::optional<Sim>* out);

private:
    struct RestoreTag {};
    Sim(const Database& db, const Level& level, RestoreTag);
    friend struct SimTestAccess;
    // construction
    void build_units(const Level& level);
    int add_unit(SimUnit u);
    // original-style helpers
    int rand_int(int n) { return static_cast<int>(rng_.unit() * n); }
    const Card& card_info(const std::string& name) const;
    void set_unit_tile(int id, bool occupy);
    void on_sb_arrived();
    void set_current_card(const std::string& card);
    void hide_range() { highlights_.clear(); }
    void show_range_for_current();
    void remove_unit(int id);
    // turns
    void show_end_turn_banner(Turn t);
    void turn_manager_whose_turn(Turn t);
    void ai_activate();
    void ai_make_decision();
    int ai_find_closest(int target);
    bool ai_check_scan_range(SimUnit& u, int dist);
    void ai_remove(int id);
    void run();
    // unit state machine
    void tick_unit(int id);
    void enter_state(SimUnit& u, UState s);
    void update_state(SimUnit& u);
    void move_enter(SimUnit& u);
    void think_enter(SimUnit& u);
    void think_update(SimUnit& u);
    void route_enter(SimUnit& u);
    void route_update(SimUnit& u);
    void attack_enter(SimUnit& u);
    void attack_update(SimUnit& u);
    void try_to_equip_shield_or_run(SimUnit& u);
    void try_to_run(SimUnit& u);
    void handle_empty_path(SimUnit& u);
    void turn_done_set(SimUnit& u, bool v);
    void set_state(SimUnit& u, UState s) { u.next_state = s; }
    void move_to(SimUnit& u, Pt dest);
    void ai_set_attack_card(SimUnit& u, const std::string& card);
    void battle_ends(SimUnit& u);
    const Pt* highlight_at(int x, int y) const;
    int closest_highlight(int unit_target, bool farthest) const;  // index or -1
    // battle manager
    void battle(int attacker, int defender);
    int pre_health_ = 0, pre_energy_ = 0;  // defender health / attacker energy just before the current exchange (for the presentation)
    void setup_animations(int damage, int antidamage, const std::string& defense);
    void end_battle();
    int map_turn_character() const;
    bool has_boss_dialogue(const std::string& type) const;
    bool has_enemy_in_barrel(const SimUnit& u);
    int create_barrel_enemy(int barrel);
    std::string dropped_card(SimUnit& u);
    std::string get_current_defense_card(SimUnit& u);
    void game_level_battle_ends();
    void check_mission_update(const SimUnit& u);
    void mission_completed(int idx);
    void remove_nick_card();
    void mark_card_used(const std::string& card);

    const Database* db_;
    Board board_;
    std::vector<SimUnit> units_;
    int sb_ = kNoUnit;
    Rng rng_;
    int stage_ = 1, level_ = 1, barrel_prob_ = 0;
    Turn turn_ = Turn::Computer;
    std::string current_card_;
    std::vector<std::string> defense_cards_;  // GameInfo._mCurrentDefenseCards
    std::array<std::vector<HandCard>, kNumCardTypes> hand_;
    std::array<std::vector<std::string>, kNumCardTypes> owned_;
    std::string nick_to_remove_;
    MissionState missions_;
    Pt flag_{};
    int hidden_unit_ = kNoUnit;
    std::vector<Pt> fountain_tiles_;
    std::vector<int> bubble_off_;
    std::vector<int> ai_list_;
    int current_unit_ = kNoUnit;  // Map.mCurrentUnit
    std::vector<Pt> highlights_;
    // battle manager state
    int attacker_ = kNoUnit, defender_ = kNoUnit, counter_attacker_ = kNoUnit;
    std::string counter_card_;
    int enemy_coin_ = 0;
    std::string enemy_card_;
    bool will_drop_ = false;
    bool gauge_pending_ = false;
    int gauge_max_ = 0;
    std::string barrel_unit_;
    bool won_ = false, lost_ = false;
    std::vector<Event> events_;
};

}  // namespace sbso::game
