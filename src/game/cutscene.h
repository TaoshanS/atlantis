// Scripted scenes (maps/game_complete.xml, minigameN.xml): the step list GameInfo.parseGameCompleteInfo builds.
#pragma once
#include <string>
#include <vector>

#include "game/dialogues.h"

namespace sbso::game {

struct CutStep {
    std::string type;       // dialogue, move, remove, cutscene, fadein, fadeout, choice, minigame
    Dialogue dialogue;      // dialogue steps; the first-time dialogue of a choice
    Dialogue shortcut;      // choice: the shortcut dialogue
    std::string actor;
    int from_x = 0, from_y = 0, to_x = 0, to_y = 0;
};

// `file` is the XML file name inside maps_dir (also used to build the localisation keys xml:<file>:speaker.text[N]).
bool load_cutscene(const std::string& maps_dir, const std::string& file, std::vector<CutStep>* out, std::string* err);

}  // namespace sbso::game
