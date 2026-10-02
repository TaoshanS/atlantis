// Stage progression on the world map: which flags unlock when a level is cleared (GameInfo.buildUnlockSequence) and the
// bookkeeping LevelSelectScreenMap does around it. Pure functions over Progress, no presentation.
#pragma once
#include <utility>
#include <vector>

#include "save/save_data.h"

namespace sbso::save {

struct UnlockStep {
    std::vector<int> unlock;  // flags (0..5) that light up when the level is cleared
    bool minigame = false;    // the bonus (white) flag unlocks too
    int dest = 0;             // flag the tour bus drives to afterwards
};

// stage 1..9, level 0..4 (level index of the cleared flag; the sixth flag ends the stage).
const UnlockStep& unlock_step(int stage, int level);
// Order in which a stage is meant to be played ("main" of the first entry).
const std::vector<int>& main_path(int stage);

// LevelSelectScreenMap.getNextHighestLevel: first UNLOCKED flag along the main path, else any UNLOCKED flag, else -1.
int next_highest_level(const Progress& p, int stage0 /*0-based*/);

struct LevelComplete {
    bool final_flag = false;            // the stage's last flag: the bus leaves for the next stage
    bool direct = false;                // nothing to animate: states are already updated, `next` is where the bus goes
    int next = -1;                      // 0-based flag to drive to (-1: terminal / none)
    std::vector<int> unlock;            // flags to animate as unlocking
    bool minigame = false;              // animate the bonus flag
};
// LevelSelectScreenMap.showLevelCompleteAni, the state part. stage 1..9, level 1..6 (GameInfo.currStage / currLevel).
LevelComplete begin_level_complete(Progress& p, int stage, int level);
// After every unlock animation finished: applies the new states and returns the flag to drive to (0-based, -1 none).
int finish_unlocks(Progress& p, int stage, int level, const LevelComplete& c);
// BUS_AT_TERMINAL: the next stage's first flag becomes playable.
void unlock_next_stage(Progress& p, int stage);
// Profile.setBonusState ignores even stages (bonus minigames only exist on odd ones).
void set_bonus_state(Progress& p, int current_stage, int stage0, int state);
// Profile.setupGameInfoToLatestLevel: {stage, level} (1-based) of the highest stage that has an UNLOCKED flag ({1,1} if none).
std::pair<int, int> latest_level(const Progress& p);
// True once the very last flag of the last stage is cleared.
bool all_stages_complete(const Progress& p);

}  // namespace sbso::save
