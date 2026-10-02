#include "progression.h"

namespace sbso::save {
namespace {

struct Stage {
    std::vector<int> main;
    UnlockStep steps[5];
};

const std::vector<Stage>& table() {
    static const std::vector<Stage> t = {
        {{0, 1, 3, 4, 5}, {{{1}, false, 1}, {{2, 3}, true, 3}, {{}, false, 3}, {{4}, false, 4}, {{5}, false, 5}}},
        {{0, 1, 5}, {{{1}, false, 1}, {{2, 4, 5}, false, 5}, {{3}, false, 3}, {{}, false, 5}, {{}, false, 5}}},
        {{0, 2, 3, 4, 5}, {{{1, 2}, true, 2}, {{}, false, 2}, {{3}, false, 3}, {{4}, false, 4}, {{5}, false, 5}}},
        {{0, 1, 2, 5}, {{{1}, false, 1}, {{2}, false, 2}, {{3, 5}, false, 5}, {{4}, false, 4}, {{}, false, 5}}},
        {{0, 1, 2, 4, 5}, {{{1}, false, 1}, {{2}, false, 2}, {{4}, false, 4}, {{}, true, 4}, {{3, 5}, false, 5}}},
        {{0, 2, 3, 4, 5}, {{{1, 2}, false, 2}, {{}, false, 2}, {{3}, false, 3}, {{4}, false, 4}, {{5}, false, 5}}},
        {{0, 1, 2, 3, 5}, {{{1}, false, 1}, {{2}, false, 2}, {{3}, false, 3}, {{4, 5}, false, 5}, {{}, true, 5}}},
        {{0, 2, 4, 5}, {{{1, 2}, false, 2}, {{}, false, 2}, {{3, 4}, false, 4}, {{}, false, 4}, {{5}, false, 5}}},
        {{0, 1, 2, 3, 4, 5}, {{{1}, false, 1}, {{2}, false, 2}, {{3}, false, 3}, {{4}, false, 4}, {{5}, false, 5}}},
    };
    return t;
}

}  // namespace

const UnlockStep& unlock_step(int stage, int level) { return table().at(stage - 1).steps[level]; }
const std::vector<int>& main_path(int stage) { return table().at(stage - 1).main; }

int next_highest_level(const Progress& p, int stage0) {
    const auto& states = p.unlocked_stages[stage0];
    for (int l : main_path(stage0 + 1))
        if (states[l] == kUnlocked) return l;
    for (int l = 0; l < kLevelsPerStage; ++l)
        if (states[l] == kUnlocked) return l;
    return -1;
}

LevelComplete begin_level_complete(Progress& p, int stage, int level) {
    LevelComplete r;
    int idx = level - 1;
    if (idx < 5) {
        const UnlockStep& s = unlock_step(stage, idx);
        if (s.unlock.empty() && !s.minigame) {
            p.unlocked_stages[stage - 1][idx] = kCleared;
            r.direct = true;
            r.next = next_highest_level(p, stage - 1);
        } else {
            r.unlock = s.unlock;
            r.minigame = s.minigame;
        }
    } else {
        p.unlocked_stages[stage - 1][idx] = kCleared;
        r.final_flag = true;
    }
    return r;
}

int finish_unlocks(Progress& p, int stage, int level, const LevelComplete& c) {
    for (int f : c.unlock) p.unlocked_stages[stage - 1][f] = kUnlocked;
    if (c.minigame) set_bonus_state(p, stage, stage - 1, kCleared);
    p.unlocked_stages[stage - 1][level - 1] = kCleared;
    int dest = unlock_step(stage, level - 1).dest;
    if (p.unlocked_stages[stage - 1][dest] == kCleared) dest = next_highest_level(p, stage - 1);
    return dest;
}

void unlock_next_stage(Progress& p, int stage) {
    if (stage < kNumStages && p.unlocked_stages[stage][0] == kLocked) p.unlocked_stages[stage][0] = kUnlocked;
}

void set_bonus_state(Progress& p, int current_stage, int stage0, int state) {
    if (current_stage % 2 == 0) return;
    p.unlocked_bonuses[stage0] = state;
}

std::pair<int, int> latest_level(const Progress& p) {
    std::pair<int, int> r{1, 1};
    for (int st = kNumStages - 1; st >= 0; --st) {
        r = {st + 1, 1};
        for (int l = 0; l < kLevelsPerStage; ++l)
            if (p.unlocked_stages[st][l] == kUnlocked) return {st + 1, l + 1};
    }
    return r;
}

bool all_stages_complete(const Progress& p) { return p.unlocked_stages[kNumStages - 1][kLevelsPerStage - 1] == kCleared; }

}  // namespace sbso::save
