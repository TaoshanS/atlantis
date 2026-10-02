// Game entry point.
//   sbso --extracted <dir> --maps <dir> [--saves <dir>] [--headless <ticks> [--shot out.png] [--click x,y@tick ...]]
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>  // SDL_main on iOS/Android (a plain main elsewhere)

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/vfs.h"
#include "app/app.h"
#include "app/screens/battle_screen.h"
#include "app/flow.h"
#include "app/screens/chest_screen.h"
#include "app/screens/scene_screens.h"
#include "app/ui/clip_screens.h"
#include "app/ui/dialogue_screen.h"
#include "app/ui/minigame_screen.h"
#include "app/screens/level_select_screen.h"
#include "app/screens/title_screen.h"

using namespace sbso::app;

#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
// Crash report: a native crash writes the signal and a backtrace to <saves>/crash.txt (the app bundle has no console).
// The file is only created when a crash happens, so the report of the last crash survives later launches.
static char g_crash_path[1024];
static void crash_handler(int sig) {
    const char* name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGABRT ? "SIGABRT" : sig == SIGBUS ? "SIGBUS" : sig == SIGILL ? "SIGILL"
                     : sig == SIGTRAP ? "SIGTRAP" : "SIGFPE";
    void* frames[64];
    int n = backtrace(frames, 64);
    int crash_fd = g_crash_path[0] ? open(g_crash_path, O_WRONLY | O_CREAT | O_TRUNC, 0644) : -1;
    for (int fd : {crash_fd, 2}) {
        if (fd < 0) continue;
        (void)!write(fd, "crash: ", 7);
        (void)!write(fd, name, strlen(name));
        (void)!write(fd, "\n", 1);
        backtrace_symbols_fd(frames, n, fd);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}
static void install_crash_handler(const std::string& dir) {
    std::string path = dir + "/crash.txt";
    if (path.size() < sizeof(g_crash_path)) std::memcpy(g_crash_path, path.c_str(), path.size() + 1);
    for (int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE, SIGTRAP}) signal(sig, crash_handler);  // AppKit asserts trap (SIGTRAP)
}
#else
static void install_crash_handler(const std::string&) {}
#endif

int main(int argc, char** argv) {
    AppOptions opt;
    bool saves_given = false;
    std::string import_sol, gain_card;
    double map_fps = -1;
    int gain_belt = 0;
    int headless_ticks = -1;
    struct Typed { std::string text; int tick; };
    struct Key { int code, tick; };
    std::vector<Typed> typed;
    std::vector<Key> keys;
    bool start_map = false, start_chest = false, just_won = false, start_intro = false, start_credits = false;
    int cleared_stage = 0, cleared_level = 0, coins = -1, minigame = -1, scene = -2, suspend_at = -1, flow_stage = 0, flow_level = 0, win_at = -1, lose_at = -1;
    std::vector<int> wins, loses;  // --win / --lose may be repeated (chains of levels)
    std::string profile_name;
    std::string shot, filter;
    int battle_stage = 0, battle_level = 1;
    struct Click { int x, y, tick; };
    struct Act { std::string card; int tx = 0, ty = 0, tick = 0; bool end_turn = false; };
    std::vector<Act> acts;
    std::vector<Click> clicks, hovers;
    struct Drag { int x0, y0, x1, y1, tick; };
    std::vector<Drag> drags;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--extracted") opt.extracted = next();
        else if (a == "--maps") opt.maps_dir = next();
        else if (a == "--import-sol") import_sol = next();
        else if (a == "--saves") { opt.save_dir = next(); saves_given = true; }
        else if (a == "--headless") { opt.headless = true; headless_ticks = std::atoi(next().c_str()); }
        else if (a == "--shot") shot = next();
        else if (a == "--window") std::sscanf(next().c_str(), "%dx%d", &opt.window_w, &opt.window_h);
        else if (a == "--filter") filter = next();
        else if (a == "--type") { std::string t = next(); size_t at = t.rfind('@'); if (at != std::string::npos) typed.push_back({t.substr(0, at), std::atoi(t.c_str() + at + 1)}); }
        else if (a == "--key") { std::string t = next(); size_t at = t.rfind('@'); if (at != std::string::npos) keys.push_back({std::atoi(t.c_str()), std::atoi(t.c_str() + at + 1)}); }
        else if (a == "--coins") coins = std::atoi(next().c_str());
        else if (a == "--minigame") minigame = std::atoi(next().c_str());
        else if (a == "--intro") start_intro = true;
        else if (a == "--credits") start_credits = true;
        else if (a == "--scene") scene = std::atoi(next().c_str());
        else if (a == "--profile") profile_name = next();
        else if (a == "--suspend") suspend_at = std::atoi(next().c_str());
        else if (a == "--flow-battle") std::sscanf(next().c_str(), "%d,%d", &flow_stage, &flow_level);
        else if (a == "--win") { win_at = std::atoi(next().c_str()); wins.push_back(win_at); }
        else if (a == "--lose") { lose_at = std::atoi(next().c_str()); loses.push_back(lose_at); }
        else if (a == "--map") start_map = true;
        else if (a == "--gain-card") gain_card = next();
        else if (a == "--gain-belt") gain_belt = std::atoi(next().c_str());
        else if (a == "--chest") start_chest = true;
        else if (a == "--cleared") std::sscanf(next().c_str(), "%d,%d", &cleared_stage, &cleared_level);
        else if (a == "--just-won") just_won = true;
        else if (a == "--skip-dialogues") opt.skip_dialogues = true;
        else if (a == "--keep-big-fish") opt.keep_big_fish = true;
        else if (a == "--battle") std::sscanf(next().c_str(), "%d,%d", &battle_stage, &battle_level);
        else if (a == "--act") { Act x{}; char card[64] = {0}; std::sscanf(next().c_str(), "%63[^,],%d,%d@%d", card, &x.tx, &x.ty, &x.tick); x.card = card; acts.push_back(x); }
        else if (a == "--end-turn") { Act x{}; std::sscanf(next().c_str(), "%d", &x.tick); x.end_turn = true; acts.push_back(x); }
        else if (a == "--map-fps") map_fps = std::atof(next().c_str());
        else if (a == "--drag") { Drag d{}; std::sscanf(next().c_str(), "%d,%d:%d,%d@%d", &d.x0, &d.y0, &d.x1, &d.y1, &d.tick); drags.push_back(d); }  // press, move, release 5 ticks later
        else if (a == "--hover") { Click c{}; std::sscanf(next().c_str(), "%d,%d@%d", &c.x, &c.y, &c.tick); hovers.push_back(c); }  // pointer move only (hover states)
        else if (a == "--click") { Click c{}; std::sscanf(next().c_str(), "%d,%d@%d", &c.x, &c.y, &c.tick); clicks.push_back(c); }
    }
    // Installed-game defaults: saves in the per-user pref dir; the data pack next to the executable/app bundle or in the pref dir
    // (mobile: the user drops game.pak there / imports it with the Files app).
    char* pref = SDL_GetPrefPath("sbso", "atlantis");
    std::string pref_dir = pref ? pref : "";
    SDL_free(pref);
    if (!saves_given) opt.save_dir = pref_dir.empty() ? "saves" : pref_dir + "saves";
    if (!opt.headless) {
        SDL_CreateDirectory(opt.save_dir.c_str());
        install_crash_handler(opt.save_dir);
    }
    if (opt.extracted.empty()) {
        const char* base = SDL_GetBasePath();
        std::vector<std::string> candidates = {std::string(base ? base : "") + "game.pak", pref_dir + "game.pak"};
        if (const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)) candidates.push_back(std::string(docs) + "game.pak");  // iOS: visible in the Files app
        candidates.push_back(std::string(base ? base : "") + "../share/sbso/game.pak");  // Linux: `cmake --install` layout
#ifdef SDL_PLATFORM_ANDROID
        if (const char* ext = SDL_GetAndroidExternalStoragePath()) candidates.push_back(std::string(ext) + "/game.pak");  // reachable over USB / Files
#endif
        for (const std::string& c : candidates)
            if (c != "game.pak" && sbso::vfs::is_pack(c)) { opt.extracted = c; break; }
        if (opt.extracted.empty() && !opt.headless) {
            std::string msg = "Game data not found. Put game.pak in:\n" + (pref_dir.empty() ? std::string(base ? base : ".") : pref_dir);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Atlantis SquareOff", msg.c_str(), nullptr);
            return 1;
        }
    }
    if (!opt.extracted.empty() && sbso::vfs::is_pack(opt.extracted)) {  // --extracted game.pak: everything (maps included) comes from the pack
        if (!sbso::vfs::mount_pack(opt.extracted, "pak:")) { std::fprintf(stderr, "cannot read pack %s\n", opt.extracted.c_str()); return 1; }
        opt.extracted = "pak:";
        if (opt.maps_dir.empty()) opt.maps_dir = "pak:/maps";
    }
    if (opt.extracted.empty() || opt.maps_dir.empty()) {
        std::puts("usage: sbso --extracted <dir | game.pak> [--maps <dir>] [--saves <dir>] [--headless <ticks> [--shot out.png] [--click x,y@tick]]");
        return 2;
    }
    App app;
    std::string err;
    if (!app.init(opt, &err)) { std::fprintf(stderr, "init failed: %s\n", err.c_str()); return 1; }
    if (!filter.empty()) app.settings().graphics_filter = filter;
    if (map_fps >= 0) app.settings().map_fps = map_fps;  // test/tuning: --map-fps N (this run only unless settings are saved later)
    // Original Flash saves: --import-sol <file>, or SBSO2Profiles.sol dropped next to the pack / in the pref dir (renamed afterwards)
    {
        std::vector<std::string> sols;
        if (!import_sol.empty()) sols.push_back(import_sol);
        else if (!opt.headless) {
            const char* base = SDL_GetBasePath();
            for (const std::string& d : {pref_dir, std::string(base ? base : "")})
                if (!d.empty() && sbso::vfs::exists(d + "SBSO2Profiles.sol")) sols.push_back(d + "SBSO2Profiles.sol");
        }
        for (const std::string& f : sols) {
            std::string msg;
            int n = app.import_sol_file(f, &msg);
            std::fprintf(n < 0 ? stderr : stdout, "%s: %s\n", f.c_str(), msg.c_str());
            if (n > 0 && import_sol.empty()) std::rename(f.c_str(), (f + ".imported").c_str());
        }
    }
    if (opt.headless) {
        app.set_tick_hook([&](App& a, int tick) {
            for (const Click& h : hovers)
                if (h.tick <= tick) a.inject_pointer({static_cast<float>(h.x), static_cast<float>(h.y)}, false, false);
            for (const Click& c : clicks) {
                if (c.tick == tick) a.inject_pointer({static_cast<float>(c.x), static_cast<float>(c.y)}, true, false);
                if (c.tick + 1 == tick) a.inject_pointer({static_cast<float>(c.x), static_cast<float>(c.y)}, false, true);
            }
            for (const Drag& d : drags) {
                if (d.tick == tick) a.inject_pointer({static_cast<float>(d.x0), static_cast<float>(d.y0)}, true, false);
                if (d.tick + 5 == tick) a.inject_pointer({static_cast<float>(d.x1), static_cast<float>(d.y1)}, false, true);
            }
            if (tick == suspend_at) a.suspend();
            bool won = std::find(wins.begin(), wins.end(), tick) != wins.end(), lost = std::find(loses.begin(), loses.end(), tick) != loses.end();
            if (won || lost) if (auto* bs = dynamic_cast<BattleScreen*>(a.current_screen())) bs->debug_end_level(won);
            for (const Typed& t : typed) if (t.tick == tick) a.inject_text(t.text);
            for (const Key& k : keys) if (k.tick == tick) a.inject_key(k.code);
        });
    }
    if (cleared_stage > 0) {  // test hook: everything before stage/level is cleared, stage/level itself is playable
        sbso::save::Progress& pr = app.progress();
        for (int st = 0; st < cleared_stage - 1; ++st)
            for (int l = 0; l < sbso::save::kLevelsPerStage; ++l) pr.unlocked_stages[st][l] = sbso::save::kCleared;
        for (int l = 0; l < cleared_level - 1; ++l) pr.unlocked_stages[cleared_stage - 1][l] = sbso::save::kCleared;
        pr.unlocked_stages[cleared_stage - 1][cleared_level - 1] = sbso::save::kUnlocked;
        app.session().stage = cleared_stage;
        app.session().level = cleared_level;
        app.session().level_just_completed = just_won;
        pr.first_run = false;
        pr.seen_card_help = pr.seen_belt_help = pr.seen_coin_help = pr.seen_flag_help = true;
    }
    if (!profile_name.empty() && app.current_profile_name() != profile_name) app.create_profile(profile_name);
    if (coins >= 0) app.progress().coins = coins;
    std::unique_ptr<Screen> first;
    if (battle_stage > 0) {
        first = std::make_unique<BattleScreen>(app, battle_stage, battle_level, 1234);
        auto it = app.dialogues().card.find("card_1");  // --battle S,N --gain-card X: the "new card" dialogue over a battle
        if (!gain_card.empty() && it != app.dialogues().card.end())
            app.open_overlay(std::make_unique<DialogueScreen>(app, it->second, DialogueScreen::Kind::GainCard, gain_card, 0, nullptr));
    }
    else if (minigame >= 0) { first = std::make_unique<ChestScreen>(app); app.open_overlay(std::make_unique<MinigameScreen>(app, minigame, nullptr)); }
    else if (start_intro) first = std::make_unique<IntroScreen>(app, nullptr);
    else if (start_credits) { first = std::make_unique<ChestScreen>(app); app.open_overlay(std::make_unique<CreditsScreen>(app, sbso::engine::Point{320, 255}, nullptr)); }
    else if (scene >= -1) first = std::make_unique<SceneScreen>(app, scene < 0 ? SceneScreen::Mode::GameComplete : SceneScreen::Mode::Minigame, scene);
    else if (flow_stage > 0) { app.session().stage = flow_stage; app.session().level = flow_level; first = std::make_unique<TitleScreen>(app, true); flow::battle(app); }
    else if (start_chest) first = std::make_unique<ChestScreen>(app);
    else if (!gain_card.empty() || gain_belt > 0) {  // test hook: the "new card / new belt earned" dialogue over the map
        first = std::make_unique<LevelSelectScreen>(app);
        const auto& d = app.dialogues();
        if (!gain_card.empty()) {
            auto it = d.card.find("card_1");
            if (it != d.card.end()) app.open_overlay(std::make_unique<DialogueScreen>(app, it->second, DialogueScreen::Kind::GainCard, gain_card, 0, nullptr));
        } else if (gain_belt <= static_cast<int>(d.belt.size())) {
            app.open_overlay(std::make_unique<DialogueScreen>(app, d.belt[gain_belt - 1], DialogueScreen::Kind::GainBelt, "", gain_belt, nullptr));
        }
    }
    else if (start_map) first = std::make_unique<LevelSelectScreen>(app);
    else { first = std::make_unique<TitleScreen>(app, false); app.switch_music("menu"); }  // sbso2.finishLoad: switchMusic("menu") before the title splash
    BattleScreen* battle = battle_stage > 0 ? static_cast<BattleScreen*>(first.get()) : nullptr;
    if (opt.headless && battle) {
        app.set_tick_hook([&, battle](App& a, int tick) {
            for (const Click& h : hovers)
                if (h.tick <= tick) a.inject_pointer({static_cast<float>(h.x), static_cast<float>(h.y)}, false, false);
            for (const Click& c : clicks) {
                if (c.tick == tick) a.inject_pointer({static_cast<float>(c.x), static_cast<float>(c.y)}, true, false);
                if (c.tick + 1 == tick) a.inject_pointer({static_cast<float>(c.x), static_cast<float>(c.y)}, false, true);
            }
            for (const Drag& d : drags) {
                if (d.tick == tick) a.inject_pointer({static_cast<float>(d.x0), static_cast<float>(d.y0)}, true, false);
                if (d.tick + 5 == tick) a.inject_pointer({static_cast<float>(d.x1), static_cast<float>(d.y1)}, false, true);
            }
            if (tick == suspend_at) a.suspend();
            for (const Typed& t : typed) if (t.tick == tick) a.inject_text(t.text);
            for (const Key& k : keys) if (k.tick == tick) a.inject_key(k.code);
            for (const Act& x : acts) {
                if (x.tick != tick) continue;
                if (x.end_turn) battle->press_end_turn();
                else if (!battle->play_card_at(x.card, {x.tx, x.ty})) std::fprintf(stderr, "act %s -> (%d,%d) refused at tick %d\n", x.card.c_str(), x.tx, x.ty, tick);
            }
        });
    }
    int rc = app.run(std::move(first), headless_ticks);
    if (opt.headless && !shot.empty() && !app.screenshot(shot)) { std::fprintf(stderr, "screenshot failed\n"); return 1; }
    return rc;
}
