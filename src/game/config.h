// Non-text data of maps/SBSO2_CONFIG.xml: stages, belts and the default profile cards.
#pragma once
#include <string>
#include <vector>

namespace sbso::game {

struct StageInfo {
    std::string name;      // e.g. "cliffs" (also the tiles/<name>.swf and <name>_<n>.xml prefix)
    int num_levels = 0;
    int barrel_prob = 0;   // percent
};

struct BeltInfo {
    std::string name;
    int capacity = 0, plus_health = 0, plus_energy = 0;
};

struct DefaultCardInfo {
    std::string type, name;
    bool in_belt = false;
};

struct MinigameInfo {
    int whammies = 0, num_cards = 0, cost = 0;
    std::string map, anim;                    // bonus level and its scripted scene
    std::vector<std::string> rares, commons;  // cards that can be won
};

struct Config {
    std::vector<StageInfo> stages;
    std::vector<BeltInfo> belts;
    std::vector<MinigameInfo> minigames;
    std::vector<DefaultCardInfo> default_cards;  // duplicates removed like GameInfo.createSBSO2_info (NICK may repeat)
    bool load(const std::string& maps_dir, std::string* err);
    // GameInfo.filePath: "<stage>_<level>.xml" (1-based indices)
    std::string level_file(int stage, int level) const;
};

}  // namespace sbso::game
