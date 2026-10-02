// Binary snapshot of the simulation (little-endian, versioned).
#include <cstring>

#include "sim.h"

namespace sbso::game {
namespace {

constexpr std::uint32_t kMagic = 0x53425353;  // "SSBS"
constexpr std::uint32_t kVersion = 1;

struct W {
    std::vector<std::uint8_t> b;
    void u8(std::uint8_t v) { b.push_back(v); }
    void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i))); }
    void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i))); }
    void i32(int v) { u32(static_cast<std::uint32_t>(v)); }
    void boolean(bool v) { u8(v ? 1 : 0); }
    void str(const std::string& s) { u32(static_cast<std::uint32_t>(s.size())); b.insert(b.end(), s.begin(), s.end()); }
    void strs(const std::vector<std::string>& v) { u32(static_cast<std::uint32_t>(v.size())); for (auto& s : v) str(s); }
    void pt(Pt p) { i32(p.x); i32(p.y); }
    void pts(const std::vector<Pt>& v) { u32(static_cast<std::uint32_t>(v.size())); for (auto& p : v) pt(p); }
    void ints(const std::vector<int>& v) { u32(static_cast<std::uint32_t>(v.size())); for (int x : v) i32(x); }
};

struct R {
    const std::vector<std::uint8_t>& b;
    size_t p = 0;
    bool ok = true;
    bool need(size_t n) { if (p + n > b.size()) ok = false; return ok; }
    std::uint8_t u8() { if (!need(1)) return 0; return b[p++]; }
    std::uint32_t u32() { if (!need(4)) return 0; std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(b[p++]) << (8 * i); return v; }
    std::uint64_t u64() { if (!need(8)) return 0; std::uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(b[p++]) << (8 * i); return v; }
    int i32() { return static_cast<int>(u32()); }
    bool boolean() { return u8() != 0; }
    std::string str() {
        std::uint32_t n = u32();
        if (!ok || n > (1u << 20) || !need(n)) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(&b[p]), n);
        p += n;
        return s;
    }
    std::uint32_t count() { std::uint32_t n = u32(); if (n > (1u << 20)) ok = false; return ok ? n : 0; }
    std::vector<std::string> strs() { std::vector<std::string> v; for (std::uint32_t n = count(), i = 0; i < n && ok; ++i) v.push_back(str()); return v; }
    Pt pt() { Pt q; q.x = i32(); q.y = i32(); return q; }
    std::vector<Pt> pts() { std::vector<Pt> v; for (std::uint32_t n = count(), i = 0; i < n && ok; ++i) v.push_back(pt()); return v; }
    std::vector<int> ints() { std::vector<int> v; for (std::uint32_t n = count(), i = 0; i < n && ok; ++i) v.push_back(i32()); return v; }
};

std::uint64_t hash_level(const Level& l) {
    std::uint64_t h = 1469598103934665603ULL;
    auto mix = [&](std::uint64_t v) { h = (h ^ v) * 1099511628211ULL; };
    mix(l.width); mix(l.height);
    for (int i = 0; i < l.width * l.height; ++i) mix(static_cast<std::uint64_t>(l.collision[i]));
    mix(l.flag_x); mix(l.flag_y);
    return h;
}

void write_unit(W& w, const SimUnit& u) {
    w.i32(u.id); w.str(u.type); w.i32(u.x); w.i32(u.y);
    w.i32(u.health); w.i32(u.max_health); w.i32(u.energy); w.i32(u.max_energy);
    w.boolean(u.alive); w.boolean(u.is_ai); w.boolean(u.attackable);
    w.u8(static_cast<std::uint8_t>(u.state));
    w.boolean(u.next_state.has_value()); w.u8(u.next_state ? static_cast<std::uint8_t>(*u.next_state) : 0);
    w.boolean(u.move_target.has_value()); w.pt(u.move_target ? *u.move_target : Pt{});
    for (const auto& c : u.cards) w.strs(c);
    w.strs(u.drop_cards); w.boolean(u.has_drop_cards); w.i32(u.drop_empties); w.i32(u.coins); w.i32(u.scan_range); w.boolean(u.belt);
    w.boolean(u.has_boss_attacks); w.strs(u.boss_attacks);
    w.boolean(u.has_instructions); w.strs(u.instructions); w.str(u.curr_attack); w.str(u.curr_move);
    w.boolean(u.attacked_this_turn); w.boolean(u.run_from_sb); w.boolean(u.turn_done);
    w.strs(u.defense_cards);
}

void read_unit(R& r, SimUnit& u) {
    u.id = r.i32(); u.type = r.str(); u.x = r.i32(); u.y = r.i32();
    u.health = r.i32(); u.max_health = r.i32(); u.energy = r.i32(); u.max_energy = r.i32();
    u.alive = r.boolean(); u.is_ai = r.boolean(); u.attackable = r.boolean();
    u.state = static_cast<UState>(r.u8());
    bool has_next = r.boolean(); auto next = static_cast<UState>(r.u8());
    if (has_next) u.next_state = next;
    bool has_target = r.boolean(); Pt target = r.pt();
    if (has_target) u.move_target = target;
    for (auto& c : u.cards) c = r.strs();
    u.drop_cards = r.strs(); u.has_drop_cards = r.boolean(); u.drop_empties = r.i32(); u.coins = r.i32(); u.scan_range = r.i32(); u.belt = r.boolean();
    u.has_boss_attacks = r.boolean(); u.boss_attacks = r.strs();
    u.has_instructions = r.boolean(); u.instructions = r.strs(); u.curr_attack = r.str(); u.curr_move = r.str();
    u.attacked_this_turn = r.boolean(); u.run_from_sb = r.boolean(); u.turn_done = r.boolean();
    u.defense_cards = r.strs();
}

}  // namespace

Sim::Sim(const Database& db, const Level& level, RestoreTag)
    : db_(&db), board_(level.width, level.height, level.collision), rng_(0) {}

std::vector<std::uint8_t> Sim::snapshot() const {
    W w;
    w.u32(kMagic); w.u32(kVersion);
    // The level hash is written by the caller-facing wrapper below through level data kept in board_.
    // (Board collision is part of the identity check.)
    {
        std::uint64_t h = 1469598103934665603ULL;
        auto mix = [&](std::uint64_t v) { h = (h ^ v) * 1099511628211ULL; };
        mix(board_.width()); mix(board_.height());
        for (int y = 0; y < board_.height(); ++y)
            for (int x = 0; x < board_.width(); ++x) mix(static_cast<std::uint64_t>(board_.tile_value(x, y)));
        mix(flag_.x); mix(flag_.y);
        w.u64(h);
    }
    w.i32(stage_); w.i32(level_); w.i32(barrel_prob_);
    auto rs = rng_.state();
    w.u64(rs.state); w.u64(rs.inc);
    w.u8(static_cast<std::uint8_t>(turn_));
    w.str(current_card_); w.strs(defense_cards_);
    for (const auto& type : hand_) {
        w.u32(static_cast<std::uint32_t>(type.size()));
        for (const auto& c : type) { w.str(c.name); w.boolean(c.used); }
    }
    for (const auto& o : owned_) w.strs(o);
    w.str(nick_to_remove_);
    for (bool b : missions_.complete) w.boolean(b);
    w.i32(missions_.kelp_remaining); w.i32(missions_.doodle_total); w.i32(missions_.which_doodle); w.i32(missions_.doodles_killed);
    for (const auto* v : {&missions_.killed_kelps, &missions_.killed_doodles}) {
        w.u32(static_cast<std::uint32_t>(v->size()));
        for (const auto& k : *v) { w.i32(k.level); w.i32(k.x); w.i32(k.y); }
    }
    w.pt(flag_); w.i32(hidden_unit_); w.pts(fountain_tiles_); w.ints(bubble_off_); w.ints(ai_list_); w.i32(current_unit_); w.pts(highlights_);
    w.i32(attacker_); w.i32(defender_); w.i32(counter_attacker_); w.str(counter_card_);
    w.i32(enemy_coin_); w.str(enemy_card_); w.boolean(will_drop_); w.boolean(gauge_pending_); w.i32(gauge_max_); w.str(barrel_unit_);
    w.boolean(won_); w.boolean(lost_);
    w.i32(sb_);
    w.u32(static_cast<std::uint32_t>(units_.size()));
    for (const auto& u : units_) write_unit(w, u);
    return w.b;
}

bool Sim::restore(const Database& db, const Level& level, const std::vector<std::uint8_t>& data, std::optional<Sim>* out) {
    R r{data};
    if (r.u32() != kMagic || r.u32() != kVersion) return false;
    if (r.u64() != hash_level(level) || !r.ok) return false;
    Sim s(db, level, RestoreTag{});
    s.stage_ = r.i32(); s.level_ = r.i32(); s.barrel_prob_ = r.i32();
    Rng::State rs; rs.state = r.u64(); rs.inc = r.u64();
    s.rng_.set_state(rs);
    s.turn_ = static_cast<Turn>(r.u8());
    s.current_card_ = r.str(); s.defense_cards_ = r.strs();
    for (auto& type : s.hand_) {
        std::uint32_t n = r.count();
        for (std::uint32_t i = 0; i < n && r.ok; ++i) { HandCard c; c.name = r.str(); c.used = r.boolean(); type.push_back(c); }
    }
    for (auto& o : s.owned_) o = r.strs();
    s.nick_to_remove_ = r.str();
    for (auto& b : s.missions_.complete) b = r.boolean();
    s.missions_.kelp_remaining = r.i32(); s.missions_.doodle_total = r.i32(); s.missions_.which_doodle = r.i32(); s.missions_.doodles_killed = r.i32();
    for (auto* v : {&s.missions_.killed_kelps, &s.missions_.killed_doodles}) {
        std::uint32_t n = r.count();
        for (std::uint32_t i = 0; i < n && r.ok; ++i) { KilledKelp k; k.level = r.i32(); k.x = r.i32(); k.y = r.i32(); v->push_back(k); }
    }
    s.flag_ = r.pt(); s.hidden_unit_ = r.i32(); s.fountain_tiles_ = r.pts(); s.bubble_off_ = r.ints(); s.ai_list_ = r.ints();
    s.current_unit_ = r.i32(); s.highlights_ = r.pts();
    s.attacker_ = r.i32(); s.defender_ = r.i32(); s.counter_attacker_ = r.i32(); s.counter_card_ = r.str();
    s.enemy_coin_ = r.i32(); s.enemy_card_ = r.str(); s.will_drop_ = r.boolean(); s.gauge_pending_ = r.boolean(); s.gauge_max_ = r.i32(); s.barrel_unit_ = r.str();
    s.won_ = r.boolean(); s.lost_ = r.boolean();
    s.sb_ = r.i32();
    std::uint32_t nunits = r.count();
    for (std::uint32_t i = 0; i < nunits && r.ok; ++i) { SimUnit u; read_unit(r, u); s.units_.push_back(std::move(u)); }
    if (!r.ok || r.p != data.size()) return false;
    // Rebuild the occupancy grid; validate ids so corrupt data cannot index out of range.
    auto valid_id = [&](int id) { return id == kNoUnit || (id >= 0 && id < static_cast<int>(s.units_.size())); };
    if (s.sb_ < 0 || s.sb_ >= static_cast<int>(s.units_.size())) return false;
    if (!valid_id(s.hidden_unit_) || !valid_id(s.current_unit_) || !valid_id(s.attacker_) || !valid_id(s.defender_) || !valid_id(s.counter_attacker_)) return false;
    for (int id : s.ai_list_) if (id < 0 || id >= static_cast<int>(s.units_.size())) return false;
    for (int id : s.bubble_off_) if (id < 0 || id >= static_cast<int>(s.units_.size())) return false;
    for (size_t i = 0; i < s.units_.size(); ++i) {
        const SimUnit& u = s.units_[i];
        if (u.id != static_cast<int>(i)) return false;
        if (u.alive && u.x >= 0 && u.y >= 0 && u.x < level.width && u.y < level.height) s.board_.set_unit(u.x, u.y, u.id);
    }
    *out = std::move(s);
    return true;
}

}  // namespace sbso::game
