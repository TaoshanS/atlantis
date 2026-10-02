// Application shell: window, renderer, audio, input, screen stack and the fixed-rate game loop.
#pragma once
#include <functional>
#include <optional>
#include <memory>
#include <string>
#include <vector>

#include "assets.h"
#include "audio/mixer.h"
#include "audio/sound_manager.h"
#include "core/layout.h"
#include "engine/movieclip.h"

#include <SDL3/SDL_platform_defines.h>
#if defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_ANDROID) || defined(SBSO_FORCE_MOBILE)  // FORCE: preview the phone UI on a desktop build
#define SBSO_MOBILE 1
#else
#define SBSO_MOBILE 0
#endif
#include "engine/sdl_renderer.h"
#include "i18n/strings.h"
#include "game/config.h"
#include "game/dialogues.h"
#include "save/store.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace sbso::app {

// Phones and tablets: always full screen and landscape, no window to resize, no QUIT (the system closes apps).
constexpr bool kMobile = SBSO_MOBILE;
constexpr float kMobileCloseScale = 1.6f;  // CLOSE buttons of the original panels, drawn bigger on phones

class App;

class Screen {
public:
    virtual ~Screen() = default;
    virtual void tick(App& app) = 0;                                      // one game frame
    virtual void draw(App& app, std::vector<engine::DrawCmd>& out) = 0;   // logical coordinates (640x480 design space)
    virtual void pointer_move(App&, engine::Point) {}
    virtual void pointer_down(App&, engine::Point) {}
    virtual void pointer_up(App&, engine::Point) {}
    virtual void key_down(App&, int /*sdl keycode*/) {}
    virtual void on_suspend(App&) {}  // the app is quitting or going to the background: persist transient state
    virtual void text_input(App&, const std::string& /*utf8*/) {}
    virtual float tick_rate() const { return 31.0f; }
    // True when the screen lays itself out over the extended logical width (battle). The others stay 4:3 and App fills the side margins.
    virtual bool uses_wide_layout() const { return false; }
    // Extra magnification the screen applies to its art (battle map on wide windows): asset resampling targets scale * zoom.
    virtual float content_zoom() const { return 1.0f; }
};

struct AppOptions {
    std::string extracted, maps_dir, save_dir;
    bool headless = false;           // dummy video driver + software renderer
    int window_w = 1280, window_h = 960;
    bool skip_dialogues = false;     // test hook: dialogues are not shown
    bool keep_big_fish = false;      // the Big Fish Games splash is removed unless requested
};

// GameInfo's mutable play state: where the player is on the world map.
struct Session {
    int stage = 1, level = 1;             // currStage / currLevel
    bool level_just_completed = false;    // mLevelJustCompletedFlag: the map plays the unlock animation
    bool help_shown = false;              // Profile.fForceShowHelp was consumed (the battle tutorial opens once per profile)
};

class App : public engine::ClipHost {
public:
    App();
    ~App();
    bool init(const AppOptions& opt, std::string* err);
    // Runs until quit; in headless mode runs `max_ticks` ticks then returns.
    int run(std::unique_ptr<Screen> first, int max_ticks = -1);
    void set_screen(std::unique_ptr<Screen> s) { pending_ = std::move(s); }
    Screen* current_screen() { return screen_.get(); }
    void quit() { quit_ = true; }
    // Overlays (OptionsScreen over the title, ...): only the top overlay ticks and receives input; the screens below keep drawing.
    void open_overlay(std::unique_ptr<Screen> s) { pending_overlay_ = std::move(s); }
    void close_overlay() { close_overlay_ = true; }
    void set_fullscreen(bool on);
    // "" = follow the system, "en" or "es-ES". Reloads the string table and stores the choice.
    void set_language(const std::string& code);
    void save_settings();

    AssetStore& assets() { return assets_; }
    audio::Mixer& mixer() { return mixer_; }
    audio::SoundManager& sound() { return *sound_; }
    save::SaveStore& store() { return *store_; }
    save::Settings& settings() { return settings_; }
    const sbso::game::Config& config() const { return config_; }
    const sbso::game::Dialogues& dialogues() const { return dialogues_; }
    save::Progress& progress() { return progress_; }  // the active save slot's progress (default profile until profiles exist)
    Session& session() { return session_; }
    // SBSO2.switchMusic: stops the current background music and starts `name` (empty = silence).
    void switch_music(const std::string& name, bool loop = true, float volume = 1.0f);
    // Active profile and the slot it was loaded from (profile screen); autosaves always go to slot 0. Without a profile the progress lives in memory only.
    const std::string& profile_id() const { return profile_id_; }
    int profile_slot() const { return slot_; }
    bool use_profile(const std::string& id, int slot);      // loads slot `slot` of the profile into progress()
    // Profile list as the original profile screen sees it (Profile.getProNames / createNewProfile / ...).
    std::vector<save::ProfileInfo> list_profiles() { return store_->list_profiles(); }
    int current_profile_index();                              // -1 without a profile
    std::string current_profile_name() const { return profile_id_.empty() ? std::string() : progress_.name; }
    bool create_profile(const std::string& name);             // creates it and makes it the active one
    bool rename_current_profile(const std::string& name);
    bool delete_current_profile();                            // falls back to the first remaining profile, if any
    bool switch_profile(int index);                           // loads the profile's autosave
    void start_text_input(bool on);                           // on-screen keyboard / SDL text events
    void inject_text(const std::string& utf8);                // test hooks
    void inject_key(int sdl_keycode);
    void suspend();                                           // quit / background: screens persist their state, autosave
    // Manual save slots 1..8 next to the autosave (slot 0).
    std::vector<save::SlotInfo> list_slots() { return profile_id_.empty() ? std::vector<save::SlotInfo>{} : store_->list_slots(profile_id_); }
    bool save_to_slot(int slot);        // current progress (+ mid-level save) -> slot
    bool load_from_slot(int slot);      // slot -> current progress; the autosave is updated too
    bool delete_slot(int slot);
    // Imports the profiles of an original SBSO2Profiles.sol as new profiles (autosave slot 0). Returns how many were created, -1 on error.
    int import_sol_file(const std::string& path, std::string* message);
    // Mid-level snapshot kept next to the autosave: one per slot, taken at a turn boundary.
    const save::SnapshotBlob* battle_snapshot() const { return snapshot_ ? &*snapshot_ : nullptr; }
    void store_battle_snapshot(int stage, int level, std::vector<std::uint8_t> data);
    void clear_battle_snapshot();
    bool autosave();                                         // writes progress() to slot 0 (the autosave); manual slots are explicit
    const AppOptions& options() const { return opt_; }
    const sbso::Layout& layout() const { return layout_; }
    i18n::Strings& strings() { return strings_; }
    const std::string& tr(const std::string& key, const std::string& english) const { return strings_.tr(key, english); }

    // ClipHost: frame scripts of every clip play sounds through here.
    void play_sound(const std::string& name, bool loop, int volume_arg) override;

    // Headless helpers (scripted input and screenshots). The hook runs before each tick.
    void set_tick_hook(std::function<void(App&, int)> h) { tick_hook_ = std::move(h); }
    void inject_pointer(engine::Point logical, bool down, bool up);
    bool screenshot(const std::string& path);
    bool render_frame();  // draws the current screen once

private:
    void update_layout();
    void snap_window_aspect();
    engine::Point to_logical(float x, float y) const;

    AppOptions opt_;
    AssetStore assets_;
    audio::Mixer mixer_;
    std::unique_ptr<audio::SoundManager> sound_;
    std::unique_ptr<save::SaveStore> store_;
    save::Settings settings_;
    i18n::Strings strings_;
    sbso::game::Config config_;
    sbso::game::Dialogues dialogues_;
    save::Progress progress_;
    Session session_;
    std::optional<save::SnapshotBlob> snapshot_;
    std::string profile_id_;
    int slot_ = 0;
    std::string music_name_;
    const engine::Library* cursor_lib_ = nullptr;
    int cursor_img_ = -1;
    engine::Point cursor_pos_{-100, -100};
    bool cursor_inside_ = false;
    std::unique_ptr<Screen> screen_, pending_;
    std::vector<std::unique_ptr<Screen>> overlays_;
    std::unique_ptr<Screen> pending_overlay_;
    bool close_overlay_ = false;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* frame_tex_ = nullptr;   // offscreen frame for the ambient margin fill of 4:3 screens on wide windows
    int frame_tex_w_ = 0, frame_tex_h_ = 0;
    SDL_Texture* blur_tex_ = nullptr;
    int blur_tex_w_ = 0, blur_tex_h_ = 0;
    std::unique_ptr<engine::SdlRenderer> gfx_;
    struct AudioOut;
    std::unique_ptr<AudioOut> audio_out_;
    sbso::Layout layout_;
    engine::Matrix view_;
    int last_out_w_ = 0, last_out_h_ = 0, stable_frames_ = 0;
    uint64_t resized_at_ns_ = 0;  // last SDL_EVENT_WINDOW_RESIZED, for snap_window_aspect
    bool asset_filter_set_ = false;
    int out_w_ = 640, out_h_ = 480;
    bool quit_ = false;
    std::uint64_t finger_id_ = 0;   // the finger that drives the pointer
    bool finger_active_ = false;
    std::function<void(App&, int)> tick_hook_;
};

}  // namespace sbso::app
