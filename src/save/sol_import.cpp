#include "sol_import.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace sbso::save {

namespace {

struct Reader {
    const std::uint8_t* p;
    size_t n, pos = 0;
    bool ok = true;
    bool need(size_t k) { if (pos + k > n) ok = false; return ok; }
    std::uint8_t u8() { return need(1) ? p[pos++] : 0; }
    std::uint16_t u16() { if (!need(2)) return 0; std::uint16_t v = static_cast<std::uint16_t>(p[pos] << 8 | p[pos + 1]); pos += 2; return v; }
    std::uint32_t u32() { if (!need(4)) return 0; std::uint32_t v = static_cast<std::uint32_t>(p[pos]) << 24 | p[pos + 1] << 16 | p[pos + 2] << 8 | p[pos + 3]; pos += 4; return v; }
    double f64() {
        if (!need(8)) return 0;
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v = v << 8 | p[pos + i];
        pos += 8;
        double d;
        std::memcpy(&d, &v, 8);
        return d;
    }
    std::string str(size_t len) { if (!need(len)) return {}; std::string s(reinterpret_cast<const char*>(p + pos), len); pos += len; return s; }
};

// --- AMF3 -------------------------------------------------------------------------------------------------------

struct Traits { std::string cls; bool dynamic = false, external = false; std::vector<std::string> names; };

struct Amf3 {
    Reader& r;
    std::vector<std::string> strings;
    std::vector<Amf> objects;      // by-reference table (copies; references are rare in these files)
    std::vector<Traits> traits;
    int depth = 0;

    std::uint32_t u29() {
        std::uint32_t v = 0;
        for (int i = 0; i < 3; ++i) {
            std::uint8_t b = r.u8();
            if (!(b & 0x80)) return v << 7 | b;
            v = v << 7 | (b & 0x7F);
        }
        return v << 8 | r.u8();
    }
    std::string string() {
        std::uint32_t h = u29();
        if (!(h & 1)) { size_t i = h >> 1; if (i >= strings.size()) { r.ok = false; return {}; } return strings[i]; }
        std::string s = r.str(h >> 1);
        if (!s.empty()) strings.push_back(s);
        return s;
    }
    Amf value() {
        Amf v;
        if (!r.ok || ++depth > 64) { r.ok = false; return v; }
        std::uint8_t t = r.u8();
        switch (t) {
            case 0: v.type = Amf::Type::Undefined; break;
            case 1: v.type = Amf::Type::Null; break;
            case 2: v.type = Amf::Type::Bool; v.b = false; break;
            case 3: v.type = Amf::Type::Bool; v.b = true; break;
            case 4: { v.type = Amf::Type::Number; std::uint32_t i = u29(); v.n = (i & 0x10000000) ? static_cast<double>(static_cast<int>(i) - 0x20000000) : static_cast<double>(i); break; }
            case 5: v.type = Amf::Type::Number; v.n = r.f64(); break;
            case 6: v.type = Amf::Type::String; v.s = string(); break;
            case 8: {  // date: treated as a number (milliseconds)
                std::uint32_t h = u29();
                if (h & 1) { v.type = Amf::Type::Number; v.n = r.f64(); objects.push_back(v); }
                else { size_t i = h >> 1; if (i < objects.size()) v = objects[i]; else r.ok = false; }
                break;
            }
            case 9: {
                std::uint32_t h = u29();
                if (!(h & 1)) { size_t i = h >> 1; if (i < objects.size()) v = objects[i]; else r.ok = false; break; }
                v.type = Amf::Type::Array;
                size_t slot = objects.size();
                objects.push_back(v);
                for (;;) {  // associative part
                    std::string k = string();
                    if (!r.ok || k.empty()) break;
                    v.props[k] = value();
                }
                size_t dense = h >> 1;
                for (size_t i = 0; i < dense && r.ok; ++i) v.items.push_back(value());
                if (slot < objects.size()) objects[slot] = v;
                break;
            }
            case 10: {
                std::uint32_t h = u29();
                if (!(h & 1)) { size_t i = h >> 1; if (i < objects.size()) v = objects[i]; else r.ok = false; break; }
                Traits tr;
                if (!(h & 2)) { size_t i = h >> 2; if (i < traits.size()) tr = traits[i]; else { r.ok = false; break; } }
                else {
                    tr.external = h & 4;
                    tr.dynamic = h & 8;
                    tr.cls = string();
                    size_t sealed = h >> 4;
                    for (size_t i = 0; i < sealed && r.ok; ++i) tr.names.push_back(string());
                    traits.push_back(tr);
                }
                if (tr.external) { r.ok = false; break; }  // custom serialisers (ByteArray proxies...) never appear in this game's profiles
                v.type = Amf::Type::Object;
                size_t slot = objects.size();
                objects.push_back(v);
                for (const auto& nme : tr.names) v.props[nme] = value();
                if (tr.dynamic)
                    for (;;) {
                        std::string k = string();
                        if (!r.ok || k.empty()) break;
                        v.props[k] = value();
                    }
                if (slot < objects.size()) objects[slot] = v;
                break;
            }
            default: r.ok = false; break;
        }
        --depth;
        return v;
    }
};

// --- AMF0 -------------------------------------------------------------------------------------------------------

struct Amf0 {
    Reader& r;
    std::vector<Amf> objects;
    int depth = 0;
    bool amf3_values = false;  // set once an avmplus marker has been seen
    Amf3* a3 = nullptr;

    std::string utf() { return r.str(r.u16()); }
    Amf value() {
        Amf v;
        if (!r.ok || ++depth > 64) { r.ok = false; return v; }
        std::uint8_t t = r.u8();
        switch (t) {
            case 0: v.type = Amf::Type::Number; v.n = r.f64(); break;
            case 1: v.type = Amf::Type::Bool; v.b = r.u8() != 0; break;
            case 2: v.type = Amf::Type::String; v.s = utf(); break;
            case 3: case 16: {
                if (t == 16) utf();  // class name
                v.type = Amf::Type::Object;
                size_t slot = objects.size();
                objects.push_back(v);
                props_until_end(&v);
                objects[slot] = v;
                break;
            }
            case 5: v.type = Amf::Type::Null; break;
            case 6: v.type = Amf::Type::Undefined; break;
            case 7: { size_t i = r.u16(); if (i < objects.size()) v = objects[i]; else r.ok = false; break; }
            case 8: {
                r.u32();  // approximate length
                v.type = Amf::Type::Array;
                size_t slot = objects.size();
                objects.push_back(v);
                props_until_end(&v);
                objects[slot] = v;
                // numeric keys are the dense part
                std::map<std::string, Amf> named;
                std::map<long, Amf> numeric;
                for (auto& kv : v.props) {
                    char* end = nullptr;
                    long idx = std::strtol(kv.first.c_str(), &end, 10);
                    if (end && *end == 0 && !kv.first.empty()) numeric[idx] = kv.second; else named[kv.first] = kv.second;
                }
                v.props = named;
                for (auto& kv : numeric) v.items.push_back(kv.second);
                objects[slot] = v;
                break;
            }
            case 10: {
                std::uint32_t n = r.u32();
                v.type = Amf::Type::Array;
                size_t slot = objects.size();
                objects.push_back(v);
                for (std::uint32_t i = 0; i < n && r.ok; ++i) v.items.push_back(value());
                objects[slot] = v;
                break;
            }
            case 11: v.type = Amf::Type::Number; v.n = r.f64(); r.u16(); break;  // date + timezone
            case 12: v.type = Amf::Type::String; v.s = r.str(r.u32()); break;
            case 17: v = a3->value(); break;  // avmplus object: the rest of this value is AMF3
            default: r.ok = false; break;
        }
        --depth;
        return v;
    }
    void props_until_end(Amf* v) {
        for (;;) {
            if (!r.ok) return;
            std::string k = utf();
            if (k.empty() && r.pos < r.n && r.p[r.pos] == 9) { r.pos++; return; }  // object end marker
            v->props[k] = value();
        }
    }
};

}  // namespace

bool parse_sol(const std::vector<std::uint8_t>& bytes, SolFile* out, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    Reader r{bytes.data(), bytes.size()};
    if (bytes.size() < 16 || r.u16() != 0x00BF) return fail("not a SOL file");
    r.u32();  // body length
    if (r.str(4) != "TCSO") return fail("missing TCSO signature");
    r.str(6);  // 00 04 00 00 00 00
    out->name = r.str(r.u16());
    std::uint32_t version = r.u32();
    if (version != 0 && version != 3) return fail("unsupported SOL encoding");
    Amf3 a3{r};
    Amf0 a0{r};
    a0.a3 = &a3;
    while (r.ok && r.pos + 2 <= bytes.size()) {
        std::string key = r.str(r.u16());
        if (!r.ok) break;
        Amf v;
        if (version == 3) { a3.depth = 0; v = a3.value(); }
        else { a0.depth = 0; v = a0.value(); }
        if (!r.ok) return fail("corrupt SOL value");
        out->data[key] = std::move(v);
        if (r.pos < bytes.size()) r.u8();  // padding byte after every property
    }
    return r.ok;
}

namespace {

int as_int(const Amf* a, int def = 0) { return a && a->type == Amf::Type::Number ? static_cast<int>(std::lround(a->n)) : (a && a->type == Amf::Type::Bool ? (a->b ? 1 : 0) : def); }
bool as_bool(const Amf* a, bool def) { return a ? (a->type == Amf::Type::Bool ? a->b : (a->type == Amf::Type::Number ? a->n != 0 : def)) : def; }
double as_num(const Amf* a, double def) { return a && a->type == Amf::Type::Number ? a->n : def; }

// Arrays may have been stored as dense arrays or as objects with numeric keys.
const Amf* element(const Amf& a, size_t i) {
    if (i < a.items.size()) return &a.items[i];
    return a.get(std::to_string(i));
}
size_t count(const Amf& a) {
    size_t n = a.items.size();
    for (size_t i = n; a.get(std::to_string(i)); ++i) ++n;
    return n;
}

std::vector<std::string> strings_of(const Amf* a) {
    std::vector<std::string> v;
    if (!a || !a->is_container()) return v;
    for (size_t i = 0, n = count(*a); i < n; ++i)
        if (const Amf* e = element(*a, i)) if (e->type == Amf::Type::String) v.push_back(e->s);
    return v;
}

std::string cut_utf8(const std::string& s, size_t max_chars) {
    size_t chars = 0, i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t l = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        if (chars == max_chars) break;
        i += l;
        ++chars;
    }
    return s.substr(0, std::min(i, s.size()));
}

std::vector<KilledUnit> killed_of(const Amf* a) {
    std::vector<KilledUnit> v;
    if (!a || !a->is_container()) return v;
    for (size_t i = 0, n = count(*a); i < n; ++i) {
        const Amf* e = element(*a, i);
        if (!e) continue;
        KilledUnit k;
        k.level = as_int(e->get("level"));
        if (const Amf* t = e->get("tile")) { k.x = as_int(t->get("x")); k.y = as_int(t->get("y")); }
        v.push_back(k);
    }
    return v;
}

}  // namespace

bool convert_profiles(const SolFile& sol, SolImport* out, std::string* err) {
    auto it = sol.data.find("profiles");
    if (it == sol.data.end() || !it->second.is_container()) { if (err) *err = "no profiles in the file"; return false; }
    const Amf& list = it->second;
    static const char* const kTypes[kNumCardTypes] = {"MOVE", "ATTACK", "DEFEND", "NICK"};
    for (size_t pi = 0, pn = count(list); pi < pn; ++pi) {
        const Amf* po = element(list, pi);
        if (!po || po->type != Amf::Type::Object) continue;
        SolProfile sp;
        Progress& p = sp.progress;
        const Amf* nm = po->get("proName");
        p.name = cut_utf8(nm && nm->type == Amf::Type::String ? nm->s : std::string(), kMaxProfileName);
        if (p.name.empty()) p.name = "PLAYER";
        p.current_belt_index = as_int(po->get("currentBeltIndex"));
        if (const Amf* gb = po->get("gainedBelts"))
            for (int i = 0; i < kNumBelts; ++i) p.gained_belts[i] = as_int(element(*gb, i));
        for (int t = 0; t < kNumCardTypes; ++t) {
            if (const Amf* c = po->get("beltedCards")) p.belted[t] = strings_of(c->get(kTypes[t]));
            if (const Amf* c = po->get("allCards")) p.all_cards[t] = strings_of(c->get(kTypes[t]));
            if (const Amf* c = po->get("newCards")) p.new_cards[t] = strings_of(c->get(kTypes[t]));
        }
        p.coins = std::max(0, std::min(999, as_int(po->get("coinCount"), 3)));
        if (const Amf* us = po->get("unlockedStages"))
            for (int s = 0; s < kNumStages; ++s)
                if (const Amf* row = element(*us, s))
                    for (int l = 0; l < kLevelsPerStage; ++l) p.unlocked_stages[s][l] = std::max(0, std::min(2, as_int(element(*row, l))));
        if (const Amf* ub = po->get("unlockedBonuses"))
            for (int i = 0; i < kNumStages; ++i) p.unlocked_bonuses[i] = as_int(element(*ub, i));
        p.encountered_enemies = strings_of(po->get("encounteredEnemies"));
        if (const Amf* cm = po->get("completedMiniMissions"))
            for (int i = 0; i < kNumStages; ++i) p.completed_missions[i] = as_bool(element(*cm, i), false);
        p.killed_kelps = killed_of(po->get("killedKelps"));
        p.killed_doodles = killed_of(po->get("killedDoodleBobs"));
        p.first_run = as_bool(po->get("firstRun"), true);
        p.seen_card_help = as_bool(po->get("seenCardHelp"), false);
        p.seen_belt_help = as_bool(po->get("seenBeltHelp"), false);
        p.seen_flag_help = as_bool(po->get("seenFlagHelp"), false);
        p.seen_coin_help = as_bool(po->get("seenCoinHelp"), false);
        if (const Amf* mp = po->get("minigamePlays"))
            for (int i = 0; i < 4; ++i) p.minigame_plays[i] = as_int(element(*mp, i));
        sp.music_vol = as_num(po->get("musicVol"), 0.6);
        sp.fx_vol = as_num(po->get("fxVol"), 0.6);
        sp.fullscreen = as_bool(po->get("fullscreen"), true);
        out->profiles.push_back(std::move(sp));
    }
    auto lp = sol.data.find("lastProfile");
    out->last_profile = lp != sol.data.end() ? as_int(&lp->second, -1) : -1;
    if (out->last_profile >= static_cast<int>(out->profiles.size())) out->last_profile = -1;
    return !out->profiles.empty();
}

}  // namespace sbso::save
