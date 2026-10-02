#include "sim.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sbso::game {
namespace {

int manhattan(int x0, int y0, int x1, int y1) { return std::abs(x0 - x1) + std::abs(y0 - y1); }

bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

}  // namespace

// ---------------------------------------------------------------------------------------------
// construction (Map.buildMap, SpongeBob/AIUnit constructors)
// ---------------------------------------------------------------------------------------------

Sim::Sim(const Database& db, const Level& level, const SimSetup& setup, std::uint64_t seed)
    : db_(&db), board_(level.width, level.height, level.collision), rng_(seed), stage_(setup.stage), level_(setup.level),
      barrel_prob_(setup.barrel_prob), owned_(setup.owned), missions_(setup.missions) {
    for (int t = 0; t < kNumCardTypes; ++t)
        for (const auto& n : setup.hand[t]) hand_[t].push_back({n, false});
    flag_ = {level.flag_x, level.flag_y};

    SimUnit sb;
    sb.type = "spongebob";
    sb.x = level.sb_x;
    sb.y = level.sb_y;
    sb.max_energy = sb.energy = 100 + setup.belt_plus_energy;
    sb.max_health = sb.health = 100 + setup.belt_plus_health;
    sb_ = add_unit(sb);

    // Stage 4, level 5: the fountain mission tiles.
    if (stage_ == 4 && !missions_.complete[stage_ - 1] && level_ == 5) {
        for (Pt p : {Pt{11, 9}, Pt{12, 10}, Pt{13, 10}, Pt{14, 9}})
            if (board_.tile_value(p.x, p.y)) fountain_tiles_.push_back(p);
    }
    build_units(level);
    {   // MapFlag is a Unit too: it occupies its tile (blocks movement and non-attackable for ranges).
        SimUnit f;
        f.type = "map_flag";
        f.x = level.flag_x;
        f.y = level.flag_y;
        f.max_health = f.health = 100;
        f.max_energy = f.energy = 100;
        f.attackable = false;
        add_unit(f);
    }
    // _startTurns: the first banner hands the turn to the player.
    show_end_turn_banner(Turn::Player);
}

int Sim::add_unit(SimUnit u) {
    u.id = static_cast<int>(units_.size());
    units_.push_back(std::move(u));
    SimUnit& r = units_.back();
    board_.set_unit(r.x, r.y, r.id);  // Unit constructor registers the tile (no arrival checks)
    Event e{Event::Kind::UnitSpawned};
    e.a = r.id;
    events_.push_back(e);
    return r.id;
}

const Card& Sim::card_info(const std::string& name) const {
    const Card* c = db_->card(name);
    if (!c) {
        if (name.empty()) throw AsError{};  // mCardInfo[null] -> TypeError in the original
        throw std::runtime_error(name + " is not in the database of cards");
    }
    return *c;
}

namespace {

bool by_damage_desc(const Database& db, const std::string& a, const std::string& b) {
    return db.card(a)->damage > db.card(b)->damage;
}

}  // namespace

void Sim::build_units(const Level& level) {
    int num_empties = 0;  // declared outside the loop in Map.buildMap, so it leaks between enemies
    bool set_hidden = false;
    for (const Spawn& sp : level.spawns) {
        // Stage specific spawn rules.
        auto killed = [&](const std::vector<KilledKelp>& v) {
            return std::any_of(v.begin(), v.end(), [&](const KilledKelp& k) { return k.level == level_ && k.x == sp.x && k.y == sp.y; });
        };
        switch (stage_) {
            case 1: case 5: case 7:
                if ((stage_ == 1 && sp.name == "squidward") || (stage_ == 5 && sp.name == "oldest_bubble") || (stage_ == 7 && sp.name == "gary")) {
                    if (missions_.complete[stage_ - 1]) continue;
                    set_hidden = true;
                }
                break;
            case 2:
                if (sp.name == "kelp" && killed(missions_.killed_kelps)) continue;
                break;
            case 8:
                if (sp.name == "doodlebob" && killed(missions_.killed_doodles)) continue;
                break;
            case 9:
                if (sp.name == "bot_plankton_red" && missions_.complete[stage_ - 1]) continue;
                break;
            case 3: case 6:
                if (sp.belt && missions_.complete[stage_ - 1]) continue;
                break;
            default: break;
        }
        const EnemyDef& def = db_->enemy(sp.name);
        SimUnit u;
        u.type = sp.name;
        u.x = sp.x;
        u.y = sp.y;
        u.is_ai = true;
        for (int t = 0; t < kNumCardTypes; ++t) u.cards[t] = def.cards[t];
        int coins = def.coins, energy = def.energy, health = def.health;
        if (sp.has_coin) {
            coins += sp.coin_add;
            if (sp.coin_rnd) coins += rand_int(sp.coin_rnd);
        }
        std::vector<std::string> drops;
        bool has_drops = false;
        if (sp.has_drop_cards || sp.drop_empties) {}
        if (sp.drop_empties) num_empties = sp.drop_empties;
        for (const auto& d : sp.drop_cards) { drops.push_back(d); has_drops = true; }
        if (!has_drops) {
            has_drops = def.has_drop_cards;
            drops = def.drop_cards;
            num_empties = 0;
            if (has_drops && !drops.empty()) num_empties = def.drop_empties;
        }
        if (coins < 0) coins = 0;
        if (sp.has_energy) {
            energy += sp.energy_add;
            if (sp.energy_rnd) energy += rand_int(sp.energy_rnd);
        }
        if (energy < 0) energy = 0;
        if (sp.has_health) {
            health += sp.health_add;
            if (sp.health_rnd) health += rand_int(sp.health_rnd);
        }
        if (health < 0) health = 0;
        u.coins = coins;
        u.max_energy = u.energy = energy;
        u.max_health = u.health = health;
        u.drop_cards = drops;
        u.has_drop_cards = has_drops;
        u.drop_empties = num_empties;
        u.scan_range = sp.scan_range;
        u.belt = sp.belt;
        if (is_friend(sp.name)) u.attackable = false;
        if (has_boss_status(sp.name)) {
            u.has_boss_attacks = true;
            for (const auto& c : u.cards[static_cast<int>(CardType::Attack)]) u.boss_attacks.push_back(c);
            for (const auto& c : u.cards[static_cast<int>(CardType::Nick)]) u.boss_attacks.push_back(c);
            std::stable_sort(u.boss_attacks.begin(), u.boss_attacks.end(), [&](const std::string& a, const std::string& b) { return by_damage_desc(*db_, a, b); });
        }
        auto& defend = u.cards[static_cast<int>(CardType::Defend)];
        std::stable_sort(defend.begin(), defend.end(), [&](const std::string& a, const std::string& b) { return by_damage_desc(*db_, a, b); });
        int id = add_unit(u);
        ai_list_.push_back(id);
        if (set_hidden) { hidden_unit_ = id; set_hidden = false; }
        if (sp.effect == "exit") bubble_off_.push_back(id);
    }
}

// ---------------------------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------------------------

bool Sim::has_boss_dialogue(const std::string& type) const { return level_ == 6 && boss_index(type) == stage_ + 1; }

void Sim::mission_completed(int idx) {
    if (idx >= 0 && idx < 9) missions_.complete[idx] = true;
    Event e{Event::Kind::MissionCompleted};
    e.a = idx;
    events_.push_back(e);
}

void Sim::check_mission_update(const SimUnit& u) {
    if (u.id == sb_ || !u.is_ai) return;
    if (!((u.type == "kelp" || u.type == "doodlebob" || u.type == "bot_plankton_red" || u.belt) && u.health == 0)) return;
    int m = stage_ - 1;
    if (missions_.complete[m]) return;  // _mMissionActive == false
    switch (m) {
        case 1:
            missions_.killed_kelps.push_back({level_, u.x, u.y});
            if (--missions_.kelp_remaining == 0) mission_completed(m);
            break;
        case 2: case 5: case 8: mission_completed(m); break;
        case 7:
            if (missions_.doodles_killed == missions_.which_doodle) mission_completed(m);
            break;
        default: break;
    }
}

void Sim::remove_unit(int id) {
    SimUnit& u = units_[id];
    if (!u.alive) return;
    board_.set_unit(u.x, u.y, kNoUnit);
    ai_remove(id);
    u.alive = false;
    check_mission_update(u);
    Event e{Event::Kind::UnitRemoved};
    e.a = id;
    events_.push_back(e);
}

void Sim::bubble_off_list() {
    for (int id : bubble_off_) remove_unit(id);
    bubble_off_.clear();
}

void Sim::ai_remove(int id) {
    auto it = std::find(ai_list_.begin(), ai_list_.end(), id);
    if (it == ai_list_.end()) return;
    *it = ai_list_.back();  // AIManager.removeUnit swaps with the last element
    ai_list_.pop_back();
}

void Sim::on_sb_arrived() {
    SimUnit& sb = units_[sb_];
    if (manhattan(flag_.x, flag_.y, sb.x, sb.y) == 1) {
        sb.state = UState::Win;
        won_ = true;
        events_.push_back({Event::Kind::LevelWon});
    } else if (hidden_unit_ != kNoUnit) {
        const SimUnit& h = units_[hidden_unit_];
        if (manhattan(h.x, h.y, sb.x, sb.y) == 1) mission_completed(stage_ - 1);
    } else if (!fountain_tiles_.empty()) {
        for (const Pt& p : fountain_tiles_) {
            if (p.x == sb.x && p.y == sb.y) {
                mission_completed(stage_ - 1);
                fountain_tiles_.clear();
                return;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// current card / highlight tiles (Map.showRange)
// ---------------------------------------------------------------------------------------------

void Sim::set_current_card(const std::string& card) {
    card_info(card);  // throws like GameInfo.setCurrentCard for unknown cards
    current_card_ = card;
    show_range_for_current();  // SB_CARD_CHANGED -> doCardChanged
}

void Sim::show_range_for_current() {
    if (current_unit_ == kNoUnit) return;
    const SimUnit& u = units_[current_unit_];
    const Card& c = card_info(current_card_);
    if (c.effect == "HEAL") return;
    highlights_.clear();
    if (c.type == CardType::Move) {
        highlights_ = board_.move_range({u.x, u.y}, c.max);
    } else if (c.type == CardType::Defend) {
        highlights_ = {{units_[sb_].x, units_[sb_].y}};
    } else {
        highlights_ = board_.attack_range({u.x, u.y}, c.max, c.min, c.effect, [&](int id) { return units_[id].attackable; });
    }
}

const Pt* Sim::highlight_at(int x, int y) const {
    for (const Pt& p : highlights_)
        if (p.x == x && p.y == y) return &p;
    return nullptr;
}

// Map.getClosestTileToUnit(sb, farthest). Keeps the original's quirks (initial value is the first tile's distance).
int Sim::closest_highlight(int unit_id, bool farthest) const {
    const SimUnit& t = units_[unit_id];
    if (highlights_.empty()) return -1;
    if (highlights_.size() == 1) return 0;
    int found = -1;
    int best = manhattan(t.x, t.y, highlights_[0].x, highlights_[0].y);
    for (size_t i = 1; i < highlights_.size(); ++i) {
        int d = manhattan(t.x, t.y, highlights_[i].x, highlights_[i].y);
        if (farthest ? d >= best : d <= best) {
            best = d;
            found = static_cast<int>(i);
        }
    }
    return found;
}

// ---------------------------------------------------------------------------------------------
// movement
// ---------------------------------------------------------------------------------------------

void Sim::move_to(SimUnit& u, Pt dest) {
    if (const Card* c = db_->card(current_card_)) u.energy = std::max(0, std::min(u.max_energy, u.energy - c->cost));
    Event en{Event::Kind::EnergyChanged};
    en.a = u.id;
    events_.push_back(en);
    std::vector<Pt> path = board_.find_path({u.x, u.y}, dest, false);
    if (path.empty()) {  // original gets stuck here; we simply stay put
        u.state = UState::Idle;
        return;
    }
    Event e{Event::Kind::UnitMoved};
    e.a = u.id;
    e.path = path;
    events_.push_back(e);
    u.move_target = dest;
    set_state(u, UState::Move);
}

// ---------------------------------------------------------------------------------------------
// state machine (tinymantis/StateMachine.as semantics: transitions happen on the next tick)
// ---------------------------------------------------------------------------------------------

void Sim::tick_unit(int id) {
    SimUnit& u = units_[id];
    if (!u.alive) return;
    if (u.next_state) {
        UState from = u.state, to = *u.next_state;
        u.next_state.reset();
        if (from == UState::Move) {  // MOVE_EXIT -> Map.handleUnitMoveExit: register the tile
            board_.set_unit(u.x, u.y, u.id);
            if (u.id == sb_) on_sb_arrived();
        }
        u.state = to;
        try { enter_state(u, to); } catch (const AsError&) {}
    }
    try { update_state(u); } catch (const AsError&) {}
}

void Sim::enter_state(SimUnit& u, UState s) {
    switch (s) {
        case UState::Move: move_enter(u); break;
        case UState::Idle:
            if (u.is_ai && u.has_instructions) set_state(u, UState::Think);  // AIUnit.IDLE_ENTER
            break;
        case UState::Think: think_enter(u); break;
        case UState::Route: route_enter(u); break;
        case UState::Attack: attack_enter(u); break;
        default: break;
    }
}

void Sim::update_state(SimUnit& u) {
    switch (u.state) {
        case UState::Move:
            // MOVE_UPDATE: the walk completes; position becomes the destination.
            if (u.move_target) {
                u.x = u.move_target->x;
                u.y = u.move_target->y;
                u.move_target.reset();
            }
            set_state(u, UState::Idle);
            break;
        case UState::Think: think_update(u); break;
        case UState::Route: route_update(u); break;
        case UState::Attack: attack_update(u); break;
        default: break;
    }
}

void Sim::move_enter(SimUnit& u) { board_.set_unit(u.x, u.y, kNoUnit); }

// ---------------------------------------------------------------------------------------------
// AI (AIUnit.as)
// ---------------------------------------------------------------------------------------------

void Sim::turn_done_set(SimUnit& u, bool v) {
    u.attacked_this_turn = false;
    u.run_from_sb = false;
    u.has_instructions = false;
    u.instructions.clear();
    u.turn_done = v;
}

void Sim::think_enter(SimUnit& u) {
    if (!u.has_instructions) {
        u.has_instructions = true;
        u.instructions.clear();
        const SimUnit& sb = units_[sb_];
        {
            Event act{Event::Kind::UnitActive};
            act.a = u.id;
            events_.push_back(act);
        }
        int dist = manhattan(u.x, u.y, sb.x, sb.y);
        auto& attack = u.cards[static_cast<int>(CardType::Attack)];
        auto& nick = u.cards[static_cast<int>(CardType::Nick)];
        auto& move = u.cards[static_cast<int>(CardType::Move)];
        if (u.has_boss_attacks) {
            if (u.boss_attacks.empty()) throw AsError{};  // "ENEMY HAS NEITHER ATTACK NOR NICK CARDS"
            for (const auto& c : u.boss_attacks) {
                if (u.energy >= card_info(c).cost) { u.curr_attack = c; break; }
            }
        } else {
            std::vector<std::string> picks;
            if (!attack.empty()) picks.push_back(attack[rand_int(static_cast<int>(attack.size()))]);
            if (!nick.empty()) picks.push_back(nick[rand_int(static_cast<int>(nick.size()))]);
            if (picks.empty()) throw AsError{};
            u.curr_attack = picks[rand_int(static_cast<int>(picks.size()))];
        }
        const Card& ac = card_info(u.curr_attack);
        if (dist > ac.max && !move.empty()) {
            std::vector<std::string> affordable;
            for (const auto& m : move)
                if (u.energy >= card_info(m).cost) affordable.push_back(m);
            if (affordable.empty()) {
                u.curr_move.clear();
                u.has_instructions = false;
                u.instructions.clear();
                return;
            }
            u.curr_move = affordable[rand_int(static_cast<int>(affordable.size()))];
            u.instructions.push_back("ROUTE");
        }
        if (ac.cost <= u.energy) u.instructions.push_back("ATTACK");
        else u.curr_attack.clear();
    }
    u.defense_cards.clear();
}

void Sim::think_update(SimUnit& u) {
    if (u.has_instructions && !u.instructions.empty()) {
        std::string s = u.instructions.front();
        u.instructions.erase(u.instructions.begin());
        set_state(u, s == "ROUTE" ? UState::Route : UState::Attack);
    } else {
        try_to_equip_shield_or_run(u);
    }
}

void Sim::try_to_equip_shield_or_run(SimUnit& u) {
    auto& defend = u.cards[static_cast<int>(CardType::Defend)];
    for (const auto& d : defend) {
        if (u.energy < card_info(d).cost) break;
        u.defense_cards.push_back(d);
    }
    if (u.attacked_this_turn && u.defense_cards.empty() && rng_.unit() >= 0.5) {
        try_to_run(u);
    } else {
        turn_done_set(u, true);
        set_state(u, UState::Idle);
        ai_make_decision();
    }
}

void Sim::try_to_run(SimUnit& u) {
    u.curr_move.clear();
    for (const auto& m : u.cards[static_cast<int>(CardType::Move)]) {
        if (u.energy >= card_info(m).cost) {
            u.attacked_this_turn = false;
            u.curr_move = m;
            break;
        }
    }
    if (!u.curr_move.empty()) {
        u.run_from_sb = true;
        set_state(u, UState::Route);
    } else {
        turn_done_set(u, true);
        set_state(u, UState::Idle);
        ai_make_decision();
    }
}

void Sim::route_enter(SimUnit& u) { set_current_card(u.curr_move); }

void Sim::route_update(SimUnit& u) {
    const SimUnit& sb = units_[sb_];
    int target = -1;
    if (u.run_from_sb) target = closest_highlight(sb_, true);
    std::vector<Pt> path;
    if (target != -1) path = board_.find_path({u.x, u.y}, highlights_[target], false);
    else path = board_.find_path({u.x, u.y}, {sb.x, sb.y}, false);
    if (path.empty()) {
        handle_empty_path(u);
        return;
    }
    const Pt* last = nullptr;
    for (const Pt& p : path) {
        const Pt* h = highlight_at(p.x, p.y);
        if (!h) break;
        last = h;
    }
    if (last) {
        Pt dest = *last;  // HighlightTile click -> Map.MOVE
        move_to(u, dest);
        hide_range();
    } else {
        hide_range();
        set_state(u, UState::Think);
    }
}

void Sim::handle_empty_path(SimUnit& u) {
    const SimUnit& sb = units_[sb_];
    const Pt* chosen = nullptr;
    int ci = closest_highlight(sb_, false);
    if (ci != -1) {
        Pt t = highlights_[ci];
        std::vector<Pt> path = board_.find_path({u.x, u.y}, t, false);
        for (const Pt& p : path) {
            const Pt* h = highlight_at(p.x, p.y);
            if (!h) break;
            chosen = h;
        }
    }
    if (chosen && manhattan(sb.x, sb.y, chosen->x, chosen->y) < manhattan(u.x, u.y, sb.x, sb.y)) {
        Pt dest = *chosen;
        move_to(u, dest);
        hide_range();
    } else {
        hide_range();
        set_state(u, UState::Think);
    }
}

void Sim::attack_enter(SimUnit& u) {
    if (card_info(u.curr_attack).cost > u.energy) {
        u.has_instructions = false;
        u.instructions.clear();
        set_state(u, UState::Think);
        return;
    }
    set_current_card(u.curr_attack);
    const SimUnit& sb = units_[sb_];
    if (!highlight_at(sb.x, sb.y)) {
        hide_range();
        set_state(u, UState::Think);
    }
}

void Sim::attack_update(SimUnit& u) {
    const SimUnit& sb = units_[sb_];
    if (highlight_at(sb.x, sb.y)) {
        u.attacked_this_turn = true;
        set_state(u, UState::Battle);
        // Map.ATTACK (computer turn): hide the range and start the battle.
        int tgt = board_.unit_at(sb.x, sb.y);
        if (tgt != kNoUnit) {
            hide_range();
            battle(current_unit_, tgt);
        }
    } else {
        hide_range();
        set_state(u, UState::Think);
    }
}

void Sim::ai_set_attack_card(SimUnit& u, const std::string& card) {
    u.curr_attack = card;
    if (card_info(card).cost <= u.energy) {
        set_current_card(card);  // Unit.setAttackCard -> GameInfo.setCurrentCard
        u.has_instructions = true;
        u.instructions = {"ATTACK"};
    } else {
        u.curr_attack.clear();
    }
}

void Sim::battle_ends(SimUnit& u) {
    if (u.is_ai) set_state(u, UState::Think);  // AIUnit.battleEnds
}

// ---------------------------------------------------------------------------------------------
// AI manager
// ---------------------------------------------------------------------------------------------

bool Sim::ai_check_scan_range(SimUnit& u, int dist) {
    int range = u.scan_range == -1 ? db_->enemy(u.type).scan_range : u.scan_range;
    if (dist < range) return true;
    turn_done_set(u, true);
    return false;
}

int Sim::ai_find_closest(int target) {
    int best_id = kNoUnit;
    int best = 100000;
    for (int id : std::vector<int>(ai_list_)) {
        SimUnit& u = units_[id];
        if (!u.turn_done && id != target && u.attackable) {
            int d = manhattan(u.x, u.y, units_[target].x, units_[target].y);
            if (d < best && ai_check_scan_range(u, d)) {
                best = d;
                best_id = id;
            }
        }
    }
    return best_id;
}

void Sim::ai_make_decision() {
    if (units_[sb_].health == 0) return;
    int u = ai_find_closest(sb_);
    if (u != kNoUnit) {
        current_unit_ = u;
        set_state(units_[u], UState::Think);
    } else {
        show_end_turn_banner(Turn::Player);
    }
}

void Sim::ai_activate() {
    for (int id : ai_list_) turn_done_set(units_[id], false);
    ai_make_decision();
}

// ---------------------------------------------------------------------------------------------
// turns (TurnManager, GameLevel.showEndTurnBanner / endTurnBanner_CB)
// ---------------------------------------------------------------------------------------------

void Sim::show_end_turn_banner(Turn t) {
    if (t != turn_ || (t == Turn::Player && ai_list_.empty())) {
        if (t == Turn::Player && units_[sb_].health == 0) return;  // no banner for a dead SpongeBob
        turn_manager_whose_turn(t);
        if (t == Turn::Computer) ai_activate();
    }
}

void Sim::turn_manager_whose_turn(Turn t) {
    if (turn_ != t) {
        for (auto it = units_.rbegin(); it != units_.rend(); ++it)
            if (it->alive) it->energy = it->max_energy;
        if (t == Turn::Player) {
            current_unit_ = sb_;
            for (auto& type : hand_)
                for (auto& c : type) c.used = false;
            defense_cards_.clear();
        }
        turn_ = t;
        Event e{Event::Kind::TurnStarted};
        e.a = static_cast<int>(t);
        events_.push_back(e);
    } else if (t == Turn::Player && ai_list_.empty()) {
        units_[sb_].energy = units_[sb_].max_energy;
        for (auto& type : hand_)
            for (auto& c : type) c.used = false;
    }
}

void Sim::run() {
    int guard = 0;
    while (!finished() && !gauge_pending_) {
        bool progressed = false;
        for (size_t i = 0; i < units_.size(); ++i) {
            SimUnit& u = units_[i];
            if (!u.alive) continue;
            bool active = u.next_state.has_value() || u.state == UState::Think || u.state == UState::Route || u.state == UState::Attack || u.state == UState::Move;
            if (!active) continue;
            tick_unit(static_cast<int>(i));
            progressed = true;
            if (finished() || gauge_pending_) return;
        }
        if (!progressed) break;
        if (++guard > 100000) throw std::runtime_error("simulation did not settle");
    }
}

// ---------------------------------------------------------------------------------------------
// battle manager
// ---------------------------------------------------------------------------------------------

std::string Sim::get_current_defense_card(SimUnit& u) {
    if (u.id == sb_) return defense_cards_.empty() ? std::string() : defense_cards_.front();
    if (!u.defense_cards.empty()) {
        std::string c = u.defense_cards.front();
        u.defense_cards.erase(u.defense_cards.begin());
        return c;
    }
    return {};
}

int Sim::create_barrel_enemy(int barrel) {
    const SimUnit& b = units_[barrel];
    const EnemyDef& def = db_->enemy(barrel_unit_);
    SimUnit u;
    u.type = barrel_unit_;
    u.x = b.x;
    u.y = b.y;
    u.is_ai = true;
    for (int t = 0; t < kNumCardTypes; ++t) u.cards[t] = def.cards[t];
    u.coins = def.coins;
    u.max_energy = u.energy = def.energy;
    u.max_health = u.health = def.health;
    u.scan_range = def.scan_range;
    auto& defend = u.cards[static_cast<int>(CardType::Defend)];
    std::stable_sort(defend.begin(), defend.end(), [&](const std::string& a, const std::string& c) { return by_damage_desc(*db_, a, c); });
    int id = add_unit(u);
    ai_list_.push_back(id);
    return id;
}

bool Sim::has_enemy_in_barrel(const SimUnit& u) {
    if (u.type == "barrel") return rand_int(100) < barrel_prob_;
    return false;
}

std::string Sim::dropped_card(SimUnit& u) {
    int idx = rand_int(static_cast<int>(u.drop_cards.size()) + u.drop_empties);
    if (idx + 1 > static_cast<int>(u.drop_cards.size())) return {};
    const std::string& name = u.drop_cards[idx];
    const Card& info = card_info(name);
    bool dup = info.type != CardType::Nick && contains(owned_[static_cast<int>(info.type)], name);
    return dup ? std::string() : name;
}

void Sim::battle(int attacker, int defender) {
    if (counter_attacker_ == attacker) {
        if (units_[attacker].type == "barrel") attacker = create_barrel_enemy(attacker);
        // setAttackCard on the counter-attacker
        if (attacker == sb_) set_current_card(counter_card_);
        else ai_set_attack_card(units_[attacker], counter_card_);
    }
    attacker_ = attacker;
    defender_ = defender;
    counter_attacker_ = kNoUnit;
    const bool sb_attacker = attacker_ == sb_;
    const Card& card = card_info(current_card_);
    int damage = card.damage, cost = card.cost, antidamage = 0;
    bool gauge = false;
    if (sb_attacker && card.counter_gauge_sec > 0) gauge = true;
    std::string defense = get_current_defense_card(units_[defender_]);
    if (!defense.empty()) {
        const Card& dc = card_info(defense);
        if (dc.counter_damage > 0) {
            counter_attacker_ = defender_;
            counter_card_ = defense;
        }
        antidamage = dc.damage;
    }
    if (!gauge) {
        SimUnit& a = units_[attacker_];
        SimUnit& d = units_[defender_];
        pre_health_ = d.health;
        pre_energy_ = a.energy;
        a.energy = std::max(0, std::min(a.max_energy, a.energy - cost));
        d.health = std::max(0, std::min(d.max_health, d.health - std::max(0, damage - antidamage)));
        setup_animations(damage, antidamage, defense);
    } else {
        gauge_pending_ = true;
        gauge_max_ = card.counter_damage;
        Event e{Event::Kind::CounterGauge};
        e.a = attacker_;
        e.c = gauge_max_;
        events_.push_back(e);
    }
}

void Sim::provide_gauge(double power) {
    if (!gauge_pending_) return;
    gauge_pending_ = false;
    power = std::max(0.0, std::min(power, static_cast<double>(gauge_max_)));
    int dmg = static_cast<int>(std::lround(power));
    SimUnit& d = units_[defender_];
    pre_health_ = d.health;
    pre_energy_ = units_[attacker_].energy;
    d.health = std::max(0, std::min(d.max_health, d.health - dmg));
    setup_animations(dmg, 0, "");
    run();
}

void Sim::gauge_timeout() {
    if (!gauge_pending_) return;
    gauge_pending_ = false;
    end_battle();
    run();
}

void Sim::setup_animations(int damage, int antidamage, const std::string& defense) {
    const bool sb_attacker = attacker_ == sb_;
    SimUnit& d = units_[defender_];
    enemy_coin_ = 0;
    enemy_card_.clear();
    will_drop_ = false;
    if (sb_attacker && d.health == 0) {
        if (stage_ == 2 && d.type == "kelp") {
            if (missions_.kelp_remaining == 1) will_drop_ = true;
        } else if (stage_ == 8 && d.type == "doodlebob") {
            if (++missions_.doodles_killed == missions_.which_doodle) will_drop_ = true;
        } else if (stage_ == 9 && d.type == "bot_plankton_red") {
            will_drop_ = true;
        } else if (stage_ == 3 && d.belt) {
            will_drop_ = true;
        } else if (stage_ == 6 && d.belt) {
            will_drop_ = true;
        } else if (d.has_drop_cards) {
            std::string c = dropped_card(d);
            if (!c.empty()) { will_drop_ = true; enemy_card_ = c; }
        } else if (d.coins) {
            will_drop_ = true;
            enemy_coin_ = d.coins;
        }
    }
    bool jelly = false;
    if (d.health == 0) {
        if (!will_drop_ && has_enemy_in_barrel(d)) {
            counter_attacker_ = defender_;
            counter_card_ = "ATTACK_PINK_JELLYFISH_1";
            barrel_unit_ = "jelly_pink";
            jelly = true;
        }
    }
    {   // The presentation needs everything about this exchange up front.
        Event e{Event::Kind::Battle};
        e.a = attacker_; e.b = defender_; e.c = damage; e.d = antidamage; e.h0 = pre_health_; e.e0 = pre_energy_; e.text = current_card_; e.text2 = defense;
        e.f = (d.health == 0 ? 1 : 0) | (enemy_coin_ != 0 ? 2 : 0) | (!enemy_card_.empty() ? 4 : 0) | ((will_drop_ && enemy_card_.empty() && enemy_coin_ == 0) ? 8 : 0) | (jelly ? 16 : 0);
        events_.push_back(e);
        if (will_drop_ && enemy_card_.empty() && enemy_coin_ == 0) {
            Event b{Event::Kind::BeltDropped};
            b.text = d.type;
            events_.push_back(b);
        }
    }
    if (antidamage != 0) {
        auto it = std::find(defense_cards_.begin(), defense_cards_.end(), defense);
        if (it != defense_cards_.end()) defense_cards_.erase(it);
        Event e{Event::Kind::ShieldConsumed};
        e.text = defense;
        events_.push_back(e);
    }
    // _showDefendHealth: SpongeBob disappears as soon as he is defeated.
    if (defender_ == sb_ && d.health == 0) remove_unit(sb_);
    end_battle();
}

int Sim::map_turn_character() const {
    bool sb_att = sb_ == attacker_;
    if (turn_ == Turn::Player) return sb_att ? attacker_ : defender_;
    return sb_att ? defender_ : attacker_;
}

void Sim::remove_nick_card() {
    if (nick_to_remove_.empty()) return;
    auto& nick = hand_[static_cast<int>(CardType::Nick)];
    auto it = std::find_if(nick.begin(), nick.end(), [&](const HandCard& c) { return c.name == nick_to_remove_; });
    if (it == nick.end()) throw std::runtime_error("could not remove NICK " + nick_to_remove_);
    nick.erase(it);
    Event e{Event::Kind::NickCardConsumed};
    e.text = nick_to_remove_;
    events_.push_back(e);
    nick_to_remove_.clear();
}

void Sim::game_level_battle_ends() {
    // GameLevel.battleEnds -> ActionGUI.battleEnds
    remove_nick_card();
}

void Sim::end_battle() {
    SimUnit& att = units_[attacker_];
    const bool boss = defender_ == sb_ ? false : has_boss_dialogue(units_[defender_].type);
    if (boss && units_[defender_].health == 0) {
        bubble_off_.clear();
        bubble_off_.push_back(defender_);
        Event e{Event::Kind::BossDefeated};
        e.a = defender_;
        events_.push_back(e);
    }
    if (attacker_ == sb_) {
        if (defender_ != kNoUnit && units_[defender_].health == 0 && !boss) {
            remove_unit(defender_);
            defender_ = kNoUnit;
        }
        if (enemy_coin_ != 0) {
            Event e{Event::Kind::CoinsGained};
            e.c = enemy_coin_;
            events_.push_back(e);
        }
    }
    if (units_[sb_].health == 0) {
        lost_ = true;
        events_.push_back({Event::Kind::LevelLost});
    }
    if (counter_attacker_ != kNoUnit && (units_[counter_attacker_].health > 0 || units_[counter_attacker_].type == "barrel")) {
        battle(counter_attacker_, attacker_);
        return;
    }
    const int saved_attacker = attacker_;
    (void)att;
    if (!enemy_card_.empty()) {
        Event e{Event::Kind::CardDropped};
        e.text = enemy_card_;
        events_.push_back(e);
        // DIALOGUE_OVER "gain_card": bubble list non-empty -> boss end dialogue, same continuation.
        int ch = map_turn_character();
        if (ch != kNoUnit) battle_ends(units_[ch]);
        else ai_make_decision();
        game_level_battle_ends();
        if (!bubble_off_.empty() && boss) { for (int id : bubble_off_) remove_unit(id); bubble_off_.clear(); }
    } else if (!boss || units_[defender_].health > 0) {
        game_level_battle_ends();
        int ch = map_turn_character();
        if (ch != kNoUnit) battle_ends(units_[ch]);
        else ai_make_decision();
    } else {
        // Boss end dialogue, then the same continuation (GameLevel.getBossEndDialogue).
        int ch = map_turn_character();
        if (ch != kNoUnit) battle_ends(units_[ch]);
        else ai_make_decision();
        game_level_battle_ends();
        for (int id : bubble_off_) remove_unit(id);
        bubble_off_.clear();
    }
    (void)saved_attacker;
    if (!boss) defender_ = kNoUnit;
}

// ---------------------------------------------------------------------------------------------
// player API
// ---------------------------------------------------------------------------------------------

bool Sim::can_act() const { return turn_ == Turn::Player && !gauge_pending_ && !finished() && units_[sb_].alive; }

bool Sim::card_available(const std::string& card) const {
    const Card* info = db_->card(card);
    if (!info) return false;
    for (const HandCard& c : hand_[static_cast<int>(info->type)]) {
        if (c.name == card && !c.used) {
            if (info->cost > units_[sb_].energy) return false;
            if (info->effect == "HEAL" && units_[sb_].health == units_[sb_].max_health) return false;
            return true;
        }
    }
    return false;
}

std::vector<Pt> Sim::range_for(const std::string& card) const {
    const Card* c = db_->card(card);
    const SimUnit& sb = units_[sb_];
    if (!c || c->effect == "HEAL") return {};
    if (c->type == CardType::Move) return board_.move_range({sb.x, sb.y}, c->max);
    if (c->type == CardType::Defend) return {{sb.x, sb.y}};
    return board_.attack_range({sb.x, sb.y}, c->max, c->min, c->effect, [&](int id) { return units_[id].attackable; });
}

void Sim::mark_card_used(const std::string& card) {
    for (auto& type : hand_)
        for (auto& c : type)
            if (c.name == card && !c.used) { c.used = true; return; }
}

bool Sim::play_move(const std::string& card, Pt dest) {
    if (!can_act() || !card_available(card)) return false;
    const Card& c = card_info(card);
    if (c.type != CardType::Move) return false;
    auto range = range_for(card);
    if (std::find(range.begin(), range.end(), dest) == range.end()) return false;
    current_unit_ = sb_;
    set_current_card(card);
    SimUnit& sb = units_[sb_];
    move_to(sb, dest);
    hide_range();
    mark_card_used(card);
    run();
    return true;
}

bool Sim::play_attack(const std::string& card, Pt target) {
    if (!can_act() || !card_available(card)) return false;
    const Card& c = card_info(card);
    if (c.type != CardType::Attack && c.type != CardType::Nick) return false;
    if (c.effect == "HEAL") return false;
    auto range = range_for(card);
    if (std::find(range.begin(), range.end(), target) == range.end()) return false;
    int tgt = board_.unit_at(target.x, target.y);
    if (tgt == kNoUnit || !units_[tgt].attackable) return false;
    current_unit_ = sb_;
    set_current_card(card);
    hide_range();
    mark_card_used(card);                       // disableCurrCard: the card is spent and a NICK card queued for removal
    if (c.type == CardType::Nick) nick_to_remove_ = card;
    battle(sb_, tgt);
    run();
    return true;
}

bool Sim::play_defend(const std::string& card) {
    if (!can_act() || !card_available(card)) return false;
    const Card& c = card_info(card);
    if (c.type != CardType::Defend) return false;
    current_unit_ = sb_;
    set_current_card(card);
    defense_cards_.push_back(card);
    SimUnit& sb = units_[sb_];
    sb.energy = std::max(0, std::min(sb.max_energy, sb.energy - c.cost));
    hide_range();
    mark_card_used(card);
    Event e{Event::Kind::ShieldEquipped};
    e.text = card;
    events_.push_back(e);
    return true;
}

bool Sim::play_heal(const std::string& card) {
    if (!can_act() || !card_available(card)) return false;
    const Card& c = card_info(card);
    if (c.type != CardType::Nick || c.effect != "HEAL") return false;
    SimUnit& sb = units_[sb_];
    current_unit_ = sb_;
    set_current_card(card);
    sb.health = std::min(sb.health + c.damage, sb.max_health);
    sb.energy = std::max(0, std::min(sb.max_energy, sb.energy - c.cost));
    Event e{Event::Kind::Healed};
    e.c = c.damage;
    events_.push_back(e);
    mark_card_used(card);
    nick_to_remove_ = card;  // setToRemoveNickCard removes HEAL cards immediately
    remove_nick_card();
    return true;
}

void Sim::end_turn() {
    if (!can_act()) return;
    hide_range();
    show_end_turn_banner(ai_list_.empty() ? Turn::Player : Turn::Computer);
    run();
}

std::vector<Event> Sim::take_events() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

}  // namespace sbso::game
