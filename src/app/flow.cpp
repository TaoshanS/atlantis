#include "flow.h"
#include "app/screens/saves_screen.h"

#include <cstdio>
#include <random>

#include "app/screens/battle_screen.h"
#include "app/screens/chest_screen.h"
#include "app/screens/level_select_screen.h"
#include "app/screens/scene_screens.h"
#include "app/screens/title_screen.h"
#include "app/ui/clip_screens.h"
#include "app/ui/minigame_screen.h"
#include "save/progression.h"

namespace sbso::app::flow {

void title(App& app, bool skip_to_menu) {
    app.switch_music("menu");
    app.set_screen(std::make_unique<TitleScreen>(app, skip_to_menu));
}

void play(App& app) {
    save::Progress& pr = app.progress();
    if (pr.first_run) {
        pr.first_run = false;
        intro(app);
    } else {
        level_select(app);
    }
}

void level_select(App& app) {
    app.autosave();
    app.switch_music("map");
    app.set_screen(std::make_unique<LevelSelectScreen>(app));
}

void after_slot_loaded(App& app) {
    // A slot with a mid-level snapshot resumes that battle directly; otherwise the player lands on the map.
    const save::SnapshotBlob* snap = app.battle_snapshot();
    if (snap && snap->stage >= 1 && snap->stage < 10 && snap->level >= 1) {
        app.session().stage = snap->stage;
        app.session().level = snap->level;
        battle(app, true);
    } else {
        level_select(app);
    }
}

void saved_games(App& app) {
    app.open_overlay(std::make_unique<SavesScreen>(app, [&app]() { after_slot_loaded(app); }));
}

void start_level(App& app) {
    Session& ss = app.session();
    if (ss.stage < 10) {
        // The bonus (white) flag is level 7 of the stage.
        if (ss.level == 7) minigame(app);
        else battle(app);
    } else {
        game_complete(app);
    }
}

void battle(App& app, bool resume) {
    Session& ss = app.session();
    char music[32];
    std::snprintf(music, sizeof music, "mu_level%dsong", ss.stage);
    app.switch_music(music);
    const save::SnapshotBlob* snap = resume ? app.battle_snapshot() : nullptr;
    if (snap && (snap->stage != ss.stage || snap->level != ss.level)) snap = nullptr;
    std::vector<std::uint8_t> data = snap ? snap->data : std::vector<std::uint8_t>{};
    auto b = std::make_unique<BattleScreen>(app, ss.stage, ss.level, std::random_device{}(), snap ? &data : nullptr);
    b->set_exit_handler([&app](BattleScreen::Exit how) {
        Session& s = app.session();
        if (how == BattleScreen::Exit::Quit) { app.autosave(); title(app, true); return; }
        if (how == BattleScreen::Exit::Victory) {
            // GameLevel.endBattleAni_CB: only a first-time clear (or the final level) makes the map play its unlock animation.
            const save::Progress& pr = app.progress();
            if (pr.unlocked_stages[s.stage - 1][s.level - 1] == save::kUnlocked || (s.level == 6 && s.stage == 9)) s.level_just_completed = true;
        }
        level_select(app);
    });
    app.set_screen(std::move(b));
}

void chest(App& app) {
    app.switch_music("chest");
    app.set_screen(std::make_unique<ChestScreen>(app));
}

void minigame(App& app) {
    // sbso2.resolveStageNum: stages 1, 3, 5, 7 own the four bonus games
    static const int kGame[8] = {-1, 0, -1, 1, -1, 2, -1, 3};
    int g = app.session().stage >= 1 && app.session().stage <= 7 ? kGame[app.session().stage] : -1;
    if (g < 0) { level_select(app); return; }
    app.set_screen(std::make_unique<SceneScreen>(app, SceneScreen::Mode::Minigame, g));
}

void minigame_from_chest(App& app, int which, std::function<void()> done) {
    app.open_overlay(std::make_unique<MinigameScreen>(app, which, std::move(done)));
}

void intro(App& app) {
    app.switch_music("introduction");
    app.set_screen(std::make_unique<IntroScreen>(app, [&app]() { level_select(app); }));
}

void game_complete(App& app) {
    app.switch_music("throne_room_music");
    app.set_screen(std::make_unique<SceneScreen>(app, SceneScreen::Mode::GameComplete));
}

}  // namespace sbso::app::flow
