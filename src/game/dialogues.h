// Dialogue scripts of maps/SBSO2_CONFIG.xml (speaker lines with a head frame and text).
#pragma once
#include <map>
#include <string>
#include <vector>

namespace sbso::game {

struct DialogueLine {
    std::string frame;  // head name, e.g. "sb_awe" or "mr_krabs"
    std::string text;   // English text
    std::string key;    // "xml:SBSO2_CONFIG.xml:speaker.text[N]" (N counts <speaker> elements in document order)
};
using Dialogue = std::vector<DialogueLine>;

struct Dialogues {
    std::map<std::pair<int, int>, Dialogue> stage_intro;  // (stage 1-based, level 1-based)
    std::map<int, Dialogue> boss, boss_end;                // stage 1-based
    std::vector<Dialogue> belt;                            // belt index (0-based, same order as beltData)
    std::map<std::string, Dialogue> card;                  // "card_1".."card_10"
    Dialogue krusty_krab;
    std::vector<Dialogue> minigame;

    bool load(const std::string& maps_dir, std::string* err);
    const Dialogue* intro_for(int stage, int level) const;
};

}  // namespace sbso::game
