// Import of the original Flash SharedObject ("SBSO2Profiles.sol"): AMF0 / AMF3 decoder plus the mapping of the
// original Profile objects onto Progress. Pure functions, no file or SDL access.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "save_data.h"

namespace sbso::save {

// Generic decoded AMF value.
struct Amf {
    enum class Type { Undefined, Null, Bool, Number, String, Array, Object } type = Type::Undefined;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Amf> items;                 // dense array part
    std::map<std::string, Amf> props;       // object properties / associative array part

    const Amf* get(const std::string& key) const {
        auto it = props.find(key);
        return it == props.end() ? nullptr : &it->second;
    }
    bool is_container() const { return type == Type::Array || type == Type::Object; }
};

struct SolFile {
    std::string name;
    std::map<std::string, Amf> data;
};

bool parse_sol(const std::vector<std::uint8_t>& bytes, SolFile* out, std::string* err);

struct SolProfile {
    Progress progress;
    double music_vol = 0.6, fx_vol = 0.6;
    bool fullscreen = true;
};

struct SolImport {
    std::vector<SolProfile> profiles;
    int last_profile = -1;
};

// Maps data.profiles / data.lastProfile of a parsed SBSO2Profiles.sol. Names are cut to kMaxProfileName characters.
bool convert_profiles(const SolFile& sol, SolImport* out, std::string* err);

}  // namespace sbso::save
