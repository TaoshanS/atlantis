// Screen flow of the game (sbso2.as): which screen follows which, music changes and autosaving.
#pragma once
#include <functional>

#include "app/app.h"

namespace sbso::app::flow {

void title(App& app, bool skip_to_menu);   // EXIT_TO_TITLESCREEN
void play(App& app);                       // PLAY_BUTTON_ACTIVATED on the title screen
void level_select(App& app);               // createLevelSelectScreen
void start_level(App& app);                // CREATE_GAMELEVEL: battle, bonus minigame or the game-complete screen
void battle(App& app, bool resume = false);  // resume: continue the mid-level save of this level
void chest(App& app);                      // CREATE_CHEST
void minigame(App& app);
void minigame_from_chest(App& app, int which, std::function<void()> done);  // the coin tab shortcuts
void intro(App& app);
void game_complete(App& app);
void saved_games(App& app);
// After a slot was loaded: resume its battle snapshot if it has one, otherwise go to the level map.
void after_slot_loaded(App& app);

}  // namespace sbso::app::flow
