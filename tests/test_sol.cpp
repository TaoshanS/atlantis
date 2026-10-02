// SOL import: hand-built AMF3 and AMF0 SharedObjects with one profile each.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "save/sol_import.h"

using namespace sbso::save;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

namespace {
using Bytes = std::vector<std::uint8_t>;
void put16(Bytes& b, unsigned v) { b.push_back(v >> 8 & 255); b.push_back(v & 255); }
void put32(Bytes& b, unsigned v) { put16(b, v >> 16); put16(b, v & 0xFFFF); }
void putstr(Bytes& b, const std::string& s) { put16(b, static_cast<unsigned>(s.size())); b.insert(b.end(), s.begin(), s.end()); }
void putdouble(Bytes& b, double d) { std::uint64_t v; std::memcpy(&v, &d, 8); for (int i = 7; i >= 0; --i) b.push_back(v >> (i * 8) & 255); }

Bytes header(const std::string& name, unsigned version) {
    Bytes b{0x00, 0xBF, 0, 0, 0, 0, 'T', 'C', 'S', 'O', 0, 4, 0, 0, 0, 0};
    putstr(b, name);
    put32(b, version);
    return b;
}
void finish(Bytes& b) { unsigned len = static_cast<unsigned>(b.size() - 6); b[2] = len >> 24; b[3] = len >> 16; b[4] = len >> 8; b[5] = len; }

// --- AMF3 writers (no references, inline strings) ---
void u29(Bytes& b, unsigned v) {
    if (v < 0x80) b.push_back(v);
    else if (v < 0x4000) { b.push_back(0x80 | (v >> 7)); b.push_back(v & 0x7F); }
    else if (v < 0x200000) { b.push_back(0x80 | (v >> 14)); b.push_back(0x80 | ((v >> 7) & 0x7F)); b.push_back(v & 0x7F); }
    else { b.push_back(0x80 | (v >> 22)); b.push_back(0x80 | ((v >> 15) & 0x7F)); b.push_back(0x80 | ((v >> 8) & 0x7F)); b.push_back(v & 0xFF); }
}
void a3str(Bytes& b, const std::string& s) { u29(b, static_cast<unsigned>(s.size() << 1 | 1)); b.insert(b.end(), s.begin(), s.end()); }
void a3int(Bytes& b, int v) { b.push_back(4); u29(b, static_cast<unsigned>(v) & 0x1FFFFFFF); }
void a3dbl(Bytes& b, double d) { b.push_back(5); putdouble(b, d); }
void a3bool(Bytes& b, bool v) { b.push_back(v ? 3 : 2); }
void a3string(Bytes& b, const std::string& s) { b.push_back(6); a3str(b, s); }
void a3array(Bytes& b, const std::vector<Bytes>& items) { b.push_back(9); u29(b, static_cast<unsigned>(items.size() << 1 | 1)); b.push_back(1); for (auto& i : items) b.insert(b.end(), i.begin(), i.end()); }
Bytes mk(void (*f)(Bytes&)) { Bytes b; f(b); return b; }
void a3obj_begin(Bytes& b) { b.push_back(10); u29(b, 0x0B); b.push_back(1); }  // dynamic, no sealed members, anonymous class
void a3prop(Bytes& b, const std::string& k) { a3str(b, k); }
void a3obj_end(Bytes& b) { b.push_back(1); }
}  // namespace

int main() {
    // AMF3 profile
    {
        Bytes b = header("SBSO2Profiles", 3);
        putstr(b, "profiles");
        b.push_back(9); u29(b, 1 << 1 | 1); b.push_back(1);   // array with one element
        a3obj_begin(b);
        a3prop(b, "proName"); a3string(b, "BOB");
        a3prop(b, "coinCount"); a3int(b, 42);
        a3prop(b, "currentBeltIndex"); a3int(b, 3);
        a3prop(b, "firstRun"); a3bool(b, false);
        a3prop(b, "musicVol"); a3dbl(b, 0.25);
        a3prop(b, "gainedBelts"); { Bytes a; a3array(a, {mk([](Bytes& x) { a3int(x, 1); }), mk([](Bytes& x) { a3int(x, 1); }), mk([](Bytes& x) { a3int(x, 0); })}); b.insert(b.end(), a.begin(), a.end()); }
        a3prop(b, "allCards");
        a3obj_begin(b);
        a3prop(b, "MOVE"); { Bytes a; a3array(a, {mk([](Bytes& x) { a3string(x, "walk"); }), mk([](Bytes& x) { a3string(x, "sprint"); })}); b.insert(b.end(), a.begin(), a.end()); }
        a3obj_end(b);
        a3prop(b, "unlockedStages");
        { Bytes row1; a3array(row1, {mk([](Bytes& x) { a3int(x, 2); }), mk([](Bytes& x) { a3int(x, 1); }), mk([](Bytes& x) { a3int(x, 0); })});
          Bytes a; a.push_back(9); u29(a, 1 << 1 | 1); a.push_back(1); a.insert(a.end(), row1.begin(), row1.end()); b.insert(b.end(), a.begin(), a.end()); }
        a3prop(b, "killedKelps");
        { Bytes a; a.push_back(9); u29(a, 1 << 1 | 1); a.push_back(1);
          a3obj_begin(a); a3prop(a, "level"); a3int(a, 4); a3prop(a, "tile"); a3obj_begin(a); a3prop(a, "x"); a3int(a, 7); a3prop(a, "y"); a3int(a, 9); a3obj_end(a); a3obj_end(a);
          b.insert(b.end(), a.begin(), a.end()); }
        a3obj_end(b);
        b.push_back(0);
        putstr(b, "lastProfile"); a3int(b, 0); b.push_back(0);
        finish(b);

        SolFile f;
        std::string err;
        CHECK(parse_sol(b, &f, &err));
        CHECK(f.name == "SBSO2Profiles");
        SolImport imp;
        CHECK(convert_profiles(f, &imp, &err));
        CHECK(imp.profiles.size() == 1 && imp.last_profile == 0);
        const Progress& p = imp.profiles[0].progress;
        CHECK(p.name == "BOB" && p.coins == 42 && p.current_belt_index == 3 && !p.first_run);
        CHECK(p.gained_belts[0] == 1 && p.gained_belts[1] == 1 && p.gained_belts[2] == 0);
        CHECK(p.all_cards[0].size() == 2 && p.all_cards[0][1] == "sprint");
        CHECK(p.unlocked_stages[0][0] == 2 && p.unlocked_stages[0][1] == 1 && p.unlocked_stages[0][2] == 0);
        CHECK(p.killed_kelps.size() == 1 && p.killed_kelps[0].level == 4 && p.killed_kelps[0].x == 7 && p.killed_kelps[0].y == 9);
        CHECK(imp.profiles[0].music_vol == 0.25);
    }
    // AMF0 profile (ECMA arrays, strict arrays, numbers)
    {
        Bytes b = header("SBSO2Profiles", 0);
        putstr(b, "profiles");
        b.push_back(10); put32(b, 1);                     // strict array
        b.push_back(3);                                    // object
        putstr(b, "proName"); b.push_back(2); putstr(b, "ANNA");
        putstr(b, "coinCount"); b.push_back(0); putdouble(b, 120);
        putstr(b, "firstRun"); b.push_back(1); b.push_back(1);
        putstr(b, "unlockedStages"); b.push_back(8); put32(b, 1);   // ECMA array with numeric keys
        putstr(b, "0"); b.push_back(10); put32(b, 2); b.push_back(0); putdouble(b, 1); b.push_back(0); putdouble(b, 2);
        put16(b, 0); b.push_back(9);
        put16(b, 0); b.push_back(9);                       // end of the profile object
        b.push_back(0);
        putstr(b, "lastProfile"); b.push_back(0); putdouble(b, 0); b.push_back(0);
        finish(b);
        SolFile f;
        std::string err;
        CHECK(parse_sol(b, &f, &err));
        SolImport imp;
        CHECK(convert_profiles(f, &imp, &err));
        const Progress& p = imp.profiles[0].progress;
        CHECK(p.name == "ANNA" && p.coins == 120 && p.first_run);
        CHECK(p.unlocked_stages[0][0] == 1 && p.unlocked_stages[0][1] == 2);
    }
    // garbage is rejected without crashing
    {
        SolFile f;
        std::string err;
        CHECK(!parse_sol(Bytes{1, 2, 3}, &f, &err));
        Bytes b = header("x", 3);
        putstr(b, "k"); b.push_back(10); b.push_back(0xFF);
        finish(b);
        CHECK(!parse_sol(b, &f, &err));
    }
    std::puts(fails ? "FAILED" : "sol ok");
    return fails ? 1 : 0;
}
