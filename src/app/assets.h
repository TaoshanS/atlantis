// Central access to the extracted assets and the game databases.
#pragma once
#include <map>
#include <memory>
#include <string>

#include "audio/sound_bank.h"
#include "engine/library.h"
#include "engine/text.h"
#include "game/database.h"

namespace sbso::app {

class AssetStore {
public:
    // extracted: output of tools/extract; maps_dir: the original game's "maps" directory.
    bool init(const std::string& extracted, const std::string& maps_dir, std::string* err);

    const engine::Library* library(const std::string& swf_key);  // e.g. "titlescreen", "tiles/cliffs"
    // Looks a class name ("battle.bubble_punch") up across all SWFs, like ApplicationDomain.getDefinition.
    // Returns the loaded library and the character id, or nullptr / -1.
    const engine::Library* find_class(const std::string& name, int* id);
    const std::string& extracted() const { return extracted_; }
    const std::string& maps_dir() const { return maps_; }
    std::string images_root() const { return extracted_ + "/images"; }
    engine::FontBank& fonts() { return fonts_; }
    engine::TextRaster& text() { return text_; }
    audio::SoundBank& sounds() { return sounds_; }
    const sbso::game::Database& db() const { return db_; }

private:
    std::string extracted_, maps_;
    std::map<std::string, std::unique_ptr<engine::Library>> libs_;
    std::map<std::string, std::pair<std::string, int>> class_index_;  // class name -> (swf key, character id)
    bool class_index_built_ = false;
    engine::FontBank fonts_;
    engine::TextRaster text_{&fonts_};
    audio::SoundBank sounds_;
    sbso::game::Database db_;
};

}  // namespace sbso::app
