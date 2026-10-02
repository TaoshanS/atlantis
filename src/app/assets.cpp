#include "assets.h"
#include "core/vfs.h"

#include <fstream>

#include "json.hpp"

namespace sbso::app {

bool AssetStore::init(const std::string& extracted, const std::string& maps_dir, std::string* err) {
    extracted_ = extracted;
    maps_ = maps_dir;
    if (!db_.load(maps_dir, err)) return false;
    fonts_.add_directory(extracted + "/fonts");
    fonts_.set_fallbacks({"unibody8black"});  // has the Spanish accents TikiMagic lacks
    sounds_.load(extracted);
    return true;
}

const engine::Library* AssetStore::library(const std::string& key) {
    auto it = libs_.find(key);
    if (it != libs_.end()) return it->second.get();
    std::string fname = key;
    for (size_t p; (p = fname.find('/')) != std::string::npos;) fname.replace(p, 1, "__");
    auto lib = std::make_unique<engine::Library>();
    std::string err;
    if (!lib->load(extracted_ + "/characters/" + fname + ".json", extracted_ + "/images/" + key, &err)) {
        libs_[key] = nullptr;
        return nullptr;
    }
    const engine::Library* p = lib.get();
    libs_[key] = std::move(lib);
    return p;
}

}  // namespace sbso::app

namespace sbso::app {

const engine::Library* AssetStore::find_class(const std::string& name, int* id) {
    if (!class_index_built_) {
        class_index_built_ = true;
        std::string text;
        vfs::read_text(extracted_ + "/manifest.json", &text);
        nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
        if (!j.is_discarded()) {
            for (auto it = j.begin(); it != j.end(); ++it) {
                if (!it.value().contains("symbols")) continue;
                for (auto sy = it.value()["symbols"].begin(); sy != it.value()["symbols"].end(); ++sy) {
                    const std::string cls = sy.value().get<std::string>();
                    if (!class_index_.count(cls)) class_index_[cls] = {it.key(), std::stoi(sy.key())};
                }
            }
        }
    }
    auto it = class_index_.find(name);
    if (it == class_index_.end()) { *id = -1; return nullptr; }
    *id = it->second.second;
    return library(it->second.first);
}

}  // namespace sbso::app
