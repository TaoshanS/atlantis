#include "store.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

#include "json.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

namespace sbso::save {
namespace {

constexpr const char* kFormat = "sbso-save";
constexpr int kVersion = 1;

// ---- JSON (de)serialisation of the data model ----

json to_json(const KilledUnit& k) { return json{{"level", k.level}, {"x", k.x}, {"y", k.y}}; }
KilledUnit killed_from(const json& j) { return {j.value("level", 0), j.value("x", 0), j.value("y", 0)}; }

json to_json(const Progress& p) {
    json j;
    j["name"] = p.name;
    j["current_belt_index"] = p.current_belt_index;
    j["gained_belts"] = p.gained_belts;
    static const char* kTypes[] = {"MOVE", "ATTACK", "DEFEND", "NICK"};
    for (int t = 0; t < kNumCardTypes; ++t) {
        j["belted"][kTypes[t]] = p.belted[t];
        j["all_cards"][kTypes[t]] = p.all_cards[t];
        j["new_cards"][kTypes[t]] = p.new_cards[t];
    }
    j["coins"] = p.coins;
    j["unlocked_stages"] = p.unlocked_stages;
    j["unlocked_bonuses"] = p.unlocked_bonuses;
    j["encountered_enemies"] = p.encountered_enemies;
    j["completed_missions"] = p.completed_missions;
    j["killed_kelps"] = json::array();
    for (auto& k : p.killed_kelps) j["killed_kelps"].push_back(to_json(k));
    j["killed_doodles"] = json::array();
    for (auto& k : p.killed_doodles) j["killed_doodles"].push_back(to_json(k));
    j["first_run"] = p.first_run;
    j["seen_help"] = json{{"card", p.seen_card_help}, {"belt", p.seen_belt_help}, {"flag", p.seen_flag_help}, {"coin", p.seen_coin_help}};
    j["minigame_plays"] = p.minigame_plays;
    j["doodle_belt_index"] = p.doodle_belt_index;
    return j;
}

template <size_t N, typename T>
void read_array(const json& j, const char* key, std::array<T, N>& out) {
    if (!j.contains(key) || !j[key].is_array()) return;
    for (size_t i = 0; i < N && i < j[key].size(); ++i) out[i] = j[key][i].get<T>();
}

Progress progress_from(const json& j) {
    Progress p;
    p.name = j.value("name", std::string());
    p.current_belt_index = j.value("current_belt_index", 0);
    read_array(j, "gained_belts", p.gained_belts);
    static const char* kTypes[] = {"MOVE", "ATTACK", "DEFEND", "NICK"};
    for (int t = 0; t < kNumCardTypes; ++t) {
        if (j.contains("belted") && j["belted"].contains(kTypes[t])) p.belted[t] = j["belted"][kTypes[t]].get<std::vector<std::string>>();
        if (j.contains("all_cards") && j["all_cards"].contains(kTypes[t])) p.all_cards[t] = j["all_cards"][kTypes[t]].get<std::vector<std::string>>();
        if (j.contains("new_cards") && j["new_cards"].contains(kTypes[t])) p.new_cards[t] = j["new_cards"][kTypes[t]].get<std::vector<std::string>>();
    }
    p.coins = std::max(0, std::min(999, j.value("coins", 0)));
    if (j.contains("unlocked_stages") && j["unlocked_stages"].is_array()) {
        for (size_t s = 0; s < kNumStages && s < j["unlocked_stages"].size(); ++s)
            for (size_t l = 0; l < kLevelsPerStage && l < j["unlocked_stages"][s].size(); ++l)
                p.unlocked_stages[s][l] = j["unlocked_stages"][s][l].get<int>();
    }
    read_array(j, "unlocked_bonuses", p.unlocked_bonuses);
    if (j.contains("encountered_enemies")) p.encountered_enemies = j["encountered_enemies"].get<std::vector<std::string>>();
    read_array(j, "completed_missions", p.completed_missions);
    if (j.contains("killed_kelps")) for (auto& k : j["killed_kelps"]) p.killed_kelps.push_back(killed_from(k));
    if (j.contains("killed_doodles")) for (auto& k : j["killed_doodles"]) p.killed_doodles.push_back(killed_from(k));
    p.first_run = j.value("first_run", true);
    if (j.contains("seen_help")) {
        p.seen_card_help = j["seen_help"].value("card", false);
        p.seen_belt_help = j["seen_help"].value("belt", false);
        p.seen_flag_help = j["seen_help"].value("flag", false);
        p.seen_coin_help = j["seen_help"].value("coin", false);
    }
    read_array(j, "minigame_plays", p.minigame_plays);
    p.doodle_belt_index = j.value("doodle_belt_index", 0);
    return p;
}

// ---- files ----

std::string wrap(const json& payload) {
    std::string body = payload.dump();
    json file;
    file["format"] = kFormat;
    file["version"] = kVersion;
    file["crc32"] = crc32(reinterpret_cast<const std::uint8_t*>(body.data()), body.size());
    file["payload"] = payload;
    return file.dump(1);
}

bool read_file(const fs::path& p, std::string* out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

bool unwrap(const std::string& text, json* payload) {
    json file = json::parse(text, nullptr, false);
    if (file.is_discarded() || !file.is_object() || file.value("format", "") != kFormat) return false;
    if (file.value("version", 0) > kVersion || !file.contains("payload")) return false;
    std::string body = file["payload"].dump();
    if (file.value("crc32", 0u) != crc32(reinterpret_cast<const std::uint8_t*>(body.data()), body.size())) return false;
    *payload = file["payload"];
    return true;
}

// Loads `p`, falling back to `p.bak` if the main file is missing or damaged.
bool load_json(const fs::path& p, json* payload) {
    std::string text;
    if (read_file(p, &text) && unwrap(text, payload)) return true;
    fs::path bak = p;
    bak += ".bak";
    if (read_file(bak, &text) && unwrap(text, payload)) return true;
    return false;
}

bool write_atomic(const fs::path& p, const std::string& content, std::string* err) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { if (err) *err = "cannot write " + tmp.string(); return false; }
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        f.flush();
        if (!f) { if (err) *err = "write failed: " + tmp.string(); return false; }
    }
    if (fs::exists(p, ec)) {
        fs::path bak = p;
        bak += ".bak";
        fs::rename(p, bak, ec);  // keep the previous good version
        if (ec) { if (err) *err = "cannot rotate backup: " + ec.message(); return false; }
    }
    fs::rename(tmp, p, ec);
    if (ec) { if (err) *err = "cannot replace file: " + ec.message(); return false; }
    return true;
}

std::int64_t now_unix() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

size_t utf8_length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

}  // namespace

std::uint32_t crc32(const std::uint8_t* data, size_t n) {
    static std::uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    std::uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::string base64_encode(const std::vector<std::uint8_t>& d) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < d.size(); i += 3) {
        std::uint32_t v = d[i] << 16;
        if (i + 1 < d.size()) v |= d[i + 1] << 8;
        if (i + 2 < d.size()) v |= d[i + 2];
        out += t[(v >> 18) & 63];
        out += t[(v >> 12) & 63];
        out += i + 1 < d.size() ? t[(v >> 6) & 63] : '=';
        out += i + 2 < d.size() ? t[v & 63] : '=';
    }
    return out;
}

bool base64_decode(const std::string& s, std::vector<std::uint8_t>* out) {
    out->clear();
    if (s.size() % 4 != 0) return false;
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    for (size_t i = 0; i < s.size(); i += 4) {
        int a = val(s[i]), b = val(s[i + 1]);
        int c = s[i + 2] == '=' ? -2 : val(s[i + 2]);
        int d = s[i + 3] == '=' ? -2 : val(s[i + 3]);
        if (a < 0 || b < 0 || c == -1 || d == -1) return false;
        if ((c == -2 && d != -2) || ((c == -2 || d == -2) && i + 4 != s.size())) return false;
        std::uint32_t v = (a << 18) | (b << 12) | ((c < 0 ? 0 : c) << 6) | (d < 0 ? 0 : d);
        out->push_back(static_cast<std::uint8_t>(v >> 16));
        if (c != -2) out->push_back(static_cast<std::uint8_t>(v >> 8));
        if (d != -2) out->push_back(static_cast<std::uint8_t>(v));
    }
    return true;
}

// ---------------------------------------------------------------------------------------------

SaveStore::SaveStore(fs::path root) : root_(std::move(root)) {}

bool SaveStore::valid_name(const std::string& name) {
    size_t n = utf8_length(name);
    if (n < 1 || n > static_cast<size_t>(kMaxProfileName)) return false;
    return std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 0x20 || c == 0x7F; });
}

bool SaveStore::valid_id(const std::string& id) {
    return !id.empty() && id.size() <= 32 && std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

fs::path SaveStore::slot_path(const std::string& id, int slot) const { return profile_dir(id) / ("slot" + std::to_string(slot) + ".json"); }

bool SaveStore::load_settings(Settings* out, std::string*) {
    *out = Settings{};
    json j;
    if (!load_json(root_ / "settings.json", &j)) return true;  // defaults on first run (or damaged file)
    out->music_vol = std::max(0.0, std::min(1.0, j.value("music_vol", 0.6)));
    out->fx_vol = std::max(0.0, std::min(1.0, j.value("fx_vol", 0.6)));
    out->fullscreen = j.value("fullscreen", true);
    out->language = j.value("language", std::string());
    out->graphics_filter = j.value("graphics_filter", std::string("lanczos"));
    out->last_profile = j.value("last_profile", std::string());
    out->map_fps = j.value("map_fps", 24.0);
    return true;
}

bool SaveStore::save_settings(const Settings& s, std::string* err) {
    json j{{"music_vol", s.music_vol}, {"fx_vol", s.fx_vol}, {"fullscreen", s.fullscreen}, {"language", s.language}, {"graphics_filter", s.graphics_filter}, {"last_profile", s.last_profile}, {"map_fps", s.map_fps}};
    return write_atomic(root_ / "settings.json", wrap(j), err);
}

std::vector<ProfileInfo> SaveStore::list_profiles() {
    std::vector<ProfileInfo> out;
    std::error_code ec;
    fs::path dir = root_ / "profiles";
    if (!fs::exists(dir, ec)) return out;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_directory() || !valid_id(e.path().filename().string())) continue;
        json j;
        if (!load_json(e.path() / "profile.json", &j)) continue;
        out.push_back({e.path().filename().string(), j.value("name", std::string()), j.value("last_slot", 0)});
    }
    std::sort(out.begin(), out.end(), [](const ProfileInfo& a, const ProfileInfo& b) { return a.id < b.id; });
    return out;
}

bool SaveStore::create_profile(const std::string& name, const std::vector<DefaultCard>& defaults, ProfileInfo* out, std::string* err) {
    if (!valid_name(name)) { if (err) *err = "invalid profile name"; return false; }
    auto existing = list_profiles();
    if (static_cast<int>(existing.size()) >= kMaxProfiles) { if (err) *err = "too many profiles"; return false; }
    int next = 1;
    for (const auto& p : existing) {
        if (p.id.size() == 5 && p.id[0] == 'p') next = std::max(next, std::atoi(p.id.c_str() + 1) + 1);
    }
    char buf[16];
    std::snprintf(buf, sizeof buf, "p%04d", next);
    ProfileInfo info{buf, name, 0};
    json pj{{"name", name}, {"last_slot", 0}};
    if (!write_atomic(profile_dir(info.id) / "profile.json", wrap(pj), err)) return false;
    Slot s;
    s.progress = new_progress(name, defaults);
    if (!save_slot(info.id, 0, s, err)) return false;
    if (out) *out = info;
    return true;
}

bool SaveStore::rename_profile(const std::string& id, const std::string& name, std::string* err) {
    if (!valid_id(id) || !valid_name(name)) { if (err) *err = "invalid id or name"; return false; }
    json j;
    if (!load_json(profile_dir(id) / "profile.json", &j)) { if (err) *err = "no such profile"; return false; }
    j["name"] = name;
    return write_atomic(profile_dir(id) / "profile.json", wrap(j), err);
}

bool SaveStore::set_last_slot(const std::string& id, int slot, std::string* err) {
    if (!valid_id(id) || slot < 0 || slot > kManualSlots) { if (err) *err = "invalid slot"; return false; }
    json j;
    if (!load_json(profile_dir(id) / "profile.json", &j)) { if (err) *err = "no such profile"; return false; }
    j["last_slot"] = slot;
    return write_atomic(profile_dir(id) / "profile.json", wrap(j), err);
}

bool SaveStore::delete_profile(const std::string& id, std::string* err) {
    if (!valid_id(id)) { if (err) *err = "invalid id"; return false; }
    std::error_code ec;
    fs::remove_all(profile_dir(id), ec);
    if (ec) { if (err) *err = ec.message(); return false; }
    return true;
}

std::vector<SlotInfo> SaveStore::list_slots(const std::string& id) {
    std::vector<SlotInfo> out;
    for (int s = 0; s <= kManualSlots; ++s) {
        SlotInfo info;
        info.slot = s;
        std::error_code ec;
        fs::path p = slot_path(id, s);
        fs::path bak = p;
        bak += ".bak";
        if (!fs::exists(p, ec) && !fs::exists(bak, ec)) { out.push_back(info); continue; }
        Slot sl;
        if (load_slot(id, s, &sl, nullptr)) {
            info.exists = true;
            info.has_snapshot = sl.snapshot.has_value();
            info.modified_unix = sl.modified_unix;
        } else {
            info.corrupt = true;
        }
        out.push_back(info);
    }
    return out;
}

bool SaveStore::load_slot(const std::string& id, int slot, Slot* out, std::string* err) {
    if (!valid_id(id) || slot < 0 || slot > kManualSlots) { if (err) *err = "invalid profile or slot"; return false; }
    json j;
    if (!load_json(slot_path(id, slot), &j)) { if (err) *err = "slot missing or damaged"; return false; }
    Slot s;
    s.progress = progress_from(j.value("progress", json::object()));
    s.modified_unix = j.value("modified", std::int64_t{0});
    if (j.contains("snapshot") && j["snapshot"].is_object()) {
        SnapshotBlob b;
        b.stage = j["snapshot"].value("stage", 0);
        b.level = j["snapshot"].value("level", 0);
        b.created = j["snapshot"].value("created", std::string());
        if (!base64_decode(j["snapshot"].value("data", std::string()), &b.data)) { if (err) *err = "snapshot damaged"; return false; }
        s.snapshot = std::move(b);
    }
    *out = std::move(s);
    return true;
}

bool SaveStore::save_slot(const std::string& id, int slot, const Slot& s, std::string* err) {
    if (!valid_id(id) || slot < 0 || slot > kManualSlots) { if (err) *err = "invalid profile or slot"; return false; }
    json j;
    j["progress"] = to_json(s.progress);
    j["modified"] = s.modified_unix ? s.modified_unix : now_unix();
    if (s.snapshot) {
        j["snapshot"] = json{{"stage", s.snapshot->stage}, {"level", s.snapshot->level}, {"created", s.snapshot->created}, {"data", base64_encode(s.snapshot->data)}};
    } else {
        j["snapshot"] = nullptr;
    }
    return write_atomic(slot_path(id, slot), wrap(j), err);
}

bool SaveStore::delete_slot(const std::string& id, int slot, std::string* err) {
    if (!valid_id(id) || slot < 0 || slot > kManualSlots) { if (err) *err = "invalid profile or slot"; return false; }
    std::error_code ec;
    fs::path p = slot_path(id, slot), bak = p, tmp = p;
    bak += ".bak";
    tmp += ".tmp";
    fs::remove(p, ec);
    fs::remove(bak, ec);
    fs::remove(tmp, ec);
    return true;
}

bool SaveStore::copy_slot(const std::string& id, int from, int to, std::string* err) {
    Slot s;
    if (!load_slot(id, from, &s, err)) return false;
    s.modified_unix = 0;  // stamped on save
    return save_slot(id, to, s, err);
}

}  // namespace sbso::save
