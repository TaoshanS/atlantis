#include "app.h"
#include "save/progression.h"
#include "save/sol_import.h"
#include "core/vfs.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <tuple>

#include "audio/sdl_audio.h"
#include "engine/draw_util.h"
#include "stb_image_write.h"

namespace sbso::app {

struct App::AudioOut {
    audio::SdlAudioOut out;
    explicit AudioOut(audio::Mixer* m) : out(m) {}
};

App::App() = default;

App::~App() {
    audio_out_.reset();
    gfx_.reset();
    if (frame_tex_) SDL_DestroyTexture(frame_tex_);
    if (blur_tex_) SDL_DestroyTexture(blur_tex_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool App::init(const AppOptions& opt, std::string* err) {
    opt_ = opt;
    if (opt.headless) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");  // fingers are handled below; no duplicate synthetic mouse events
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (kMobile) SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    // ~44 pt (Apple's minimum touch target) is about 54 logical px on a phone: small original buttons get a 10 px margin
    if (kMobile) engine::ButtonHit::slop = 10.0f;
    if (!SDL_Init(SDL_INIT_VIDEO)) { if (err) *err = SDL_GetError(); return false; }
#ifdef SBSO_FORCE_MOBILE
    const bool phone_window = false;  // desktop preview of the phone UI: keep a window of the requested size
#else
    const bool phone_window = kMobile;
#endif
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | (phone_window ? SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS : SDL_WINDOW_RESIZABLE);
    window_ = SDL_CreateWindow("SpongeBob SquarePants: Atlantis SquareOff", opt.window_w, opt.window_h, flags);
    if (!window_) { if (err) *err = SDL_GetError(); return false; }
    // No SDL_SetWindowAspectRatio: with min != max SDL 3.2 sets a 0x0 content aspect ratio on Cocoa and macOS 26 traps inside
    // AppKit when the window is resized, zoomed or leaves fullscreen. compute_layout caps the scene at 2.2:1 and App draws side bars.
    renderer_ = SDL_CreateRenderer(window_, opt.headless ? "software" : nullptr);
    if (!renderer_) { if (err) *err = SDL_GetError(); return false; }
    if (!opt.headless) SDL_SetRenderVSync(renderer_, 1);
    if (!assets_.init(opt.extracted, opt.maps_dir, err)) return false;
    if (!config_.load(opt.maps_dir, err)) return false;
    if (!dialogues_.load(opt.maps_dir, err)) return false;
    {
        std::vector<save::DefaultCard> defaults;
        for (const auto& d : config_.default_cards) defaults.push_back({d.type, d.name, d.in_belt});
        progress_ = save::new_progress("", defaults);
    }
    store_ = std::make_unique<save::SaveStore>(opt.save_dir);
    store_->load_settings(&settings_, nullptr);
    {
        std::string locale;
        int n = 0;
        if (SDL_Locale** locs = SDL_GetPreferredLocales(&n)) {
            if (n > 0 && locs[0]->language) locale = std::string(locs[0]->language) + (locs[0]->country ? std::string("-") + locs[0]->country : "");
            SDL_free(locs);
        }
        std::string lang = i18n::Strings::resolve(settings_.language, locale);
        if (lang != "en") strings_.load_language(opt.extracted + "/strings", lang);
    }
    {
        auto profs = store_->list_profiles();
        std::string pick;
        for (const auto& pi : profs)
            if (pi.id == settings_.last_profile) pick = pi.id;
        if (pick.empty() && !profs.empty()) pick = profs.front().id;
        if (!pick.empty()) use_profile(pick, 0);
    }
    sound_ = std::make_unique<audio::SoundManager>(&mixer_, &assets_.sounds(), 31.0f);
    sound_->set_effects_volume(static_cast<float>(settings_.fx_vol));
    sound_->set_all_music_volume(static_cast<float>(settings_.music_vol));
    gfx_ = std::make_unique<engine::SdlRenderer>(renderer_, assets_.images_root());
    gfx_->set_text(&assets_.text());
    if (!opt.headless) {
        audio_out_ = std::make_unique<AudioOut>(&mixer_);
        std::string aerr;
        if (!audio_out_->out.start(&aerr)) audio_out_.reset();  // run silent if there is no audio device
    }
    cursor_lib_ = assets_.library("main");
    if (cursor_lib_) {
        cursor_img_ = cursor_lib_->symbol_id("GameCursor_finger");
        if (cursor_img_ >= 0 && !opt.headless) SDL_HideCursor();
    }
    update_layout();
    return true;
}

void App::switch_music(const std::string& name, bool loop, float volume) {
    if (!music_name_.empty()) sound_->stop_music(music_name_);
    music_name_ = name;
    if (!name.empty()) sound_->play_music(name, loop, volume);
}

namespace {
std::vector<save::DefaultCard> default_cards(const sbso::game::Config& c) {
    std::vector<save::DefaultCard> d;
    for (const auto& x : c.default_cards) d.push_back({x.type, x.name, x.in_belt});
    return d;
}
}  // namespace

int App::current_profile_index() {
    auto l = store_->list_profiles();
    for (size_t i = 0; i < l.size(); ++i)
        if (l[i].id == profile_id_) return static_cast<int>(i);
    return -1;
}

bool App::create_profile(const std::string& name) {
    save::ProfileInfo info;
    std::string err;
    if (!store_->create_profile(name, default_cards(config_), &info, &err)) return false;
    settings_.last_profile = info.id;
    save_settings();
    return use_profile(info.id, 0);
}

bool App::rename_current_profile(const std::string& name) {
    if (profile_id_.empty()) return create_profile(name);
    if (!store_->rename_profile(profile_id_, name, nullptr)) return false;
    progress_.name = name;
    autosave();
    return true;
}

bool App::delete_current_profile() {
    if (profile_id_.empty()) return false;
    store_->delete_profile(profile_id_, nullptr);
    profile_id_.clear();
    auto rest = store_->list_profiles();
    if (!rest.empty()) return use_profile(rest.front().id, 0) && (settings_.last_profile = rest.front().id, save_settings(), true);
    progress_ = save::new_progress("", default_cards(config_));
    std::tie(session_.stage, session_.level) = save::latest_level(progress_);
    settings_.last_profile.clear();
    save_settings();
    return true;
}

bool App::switch_profile(int index) {
    auto l = store_->list_profiles();
    if (index < 0 || index >= static_cast<int>(l.size())) return false;
    settings_.last_profile = l[index].id;
    save_settings();
    return use_profile(l[index].id, 0);
}

void App::start_text_input(bool on) {
    if (!window_ || opt_.headless) return;
    if (on) SDL_StartTextInput(window_);
    else SDL_StopTextInput(window_);
}

void App::inject_key(int key) {
    Screen* s = overlays_.empty() ? screen_.get() : overlays_.back().get();
    if (s) s->key_down(*this, key);
}

void App::inject_text(const std::string& utf8) {
    Screen* s = overlays_.empty() ? screen_.get() : overlays_.back().get();
    if (s) s->text_input(*this, utf8);
}

bool App::use_profile(const std::string& id, int slot) {
    save::Slot s;
    std::string err;
    if (!store_->load_slot(id, slot, &s, &err)) return false;
    progress_ = s.progress;
    snapshot_ = s.snapshot;
    std::tie(session_.stage, session_.level) = save::latest_level(progress_);
    session_.level_just_completed = false;
    session_.help_shown = false;
    profile_id_ = id;
    slot_ = slot;
    store_->set_last_slot(id, slot, nullptr);
    return true;
}

int App::import_sol_file(const std::string& path, std::string* message) {
    auto fail = [&](const std::string& m) { if (message) *message = m; return -1; };
    std::vector<unsigned char> bytes;
    if (!vfs::read(path, &bytes)) return fail("cannot read " + path);
    save::SolFile sol;
    std::string err;
    if (!save::parse_sol(bytes, &sol, &err)) return fail(err);
    save::SolImport imp;
    if (!save::convert_profiles(sol, &imp, &err)) return fail(err);
    int created = 0;
    std::string last_id;
    for (size_t i = 0; i < imp.profiles.size(); ++i) {
        save::ProfileInfo info;
        if (!store_->create_profile(imp.profiles[i].progress.name, default_cards(config_), &info, &err)) break;
        save::Slot s;
        s.progress = imp.profiles[i].progress;
        if (!store_->save_slot(info.id, 0, s, &err)) break;
        ++created;
        if (static_cast<int>(i) == imp.last_profile || last_id.empty()) last_id = info.id;
    }
    if (created == 0) return fail(err.empty() ? "nothing imported" : err);
    if (profile_id_.empty() && !last_id.empty()) {  // first run: continue with the player's last profile
        settings_.last_profile = last_id;
        save_settings();
        use_profile(last_id, 0);
    }
    if (message) *message = std::to_string(created) + " profile(s) imported";
    return created;
}

bool App::save_to_slot(int slot) {
    if (profile_id_.empty() || slot < 1 || slot > save::kManualSlots) return false;
    save::Slot s;
    s.progress = progress_;
    s.snapshot = snapshot_;
    return store_->save_slot(profile_id_, slot, s, nullptr);
}

bool App::load_from_slot(int slot) {
    if (profile_id_.empty() || slot < 0 || slot > save::kManualSlots) return false;
    save::Slot s;
    if (!store_->load_slot(profile_id_, slot, &s, nullptr)) return false;
    progress_ = s.progress;
    snapshot_ = s.snapshot;
    std::tie(session_.stage, session_.level) = save::latest_level(progress_);
    session_.level_just_completed = false;
    if (slot != 0) autosave();
    return true;
}

bool App::delete_slot(int slot) {
    if (profile_id_.empty() || slot < 1 || slot > save::kManualSlots) return false;
    return store_->delete_slot(profile_id_, slot, nullptr);
}

void App::store_battle_snapshot(int stage, int level, std::vector<std::uint8_t> data) {
    save::SnapshotBlob b;
    b.stage = stage;
    b.level = level;
    b.data = std::move(data);
    snapshot_ = std::move(b);
    autosave();
}

void App::clear_battle_snapshot() {
    if (!snapshot_) return;
    snapshot_.reset();
    autosave();
}

bool App::autosave() {
    if (profile_id_.empty()) return false;
    save::Slot s;
    std::string err;
    store_->load_slot(profile_id_, 0, &s, nullptr);  // keeps the mid-level snapshot of the autosave slot, if any
    s.progress = progress_;
    s.snapshot = snapshot_;
    return store_->save_slot(profile_id_, 0, s, &err);
}

void App::suspend() {
    for (auto& o : overlays_) o->on_suspend(*this);
    if (screen_) screen_->on_suspend(*this);
    autosave();
}

void App::set_fullscreen(bool on) {
    if (kMobile) on = true;  // there is no windowed mode on a phone
    settings_.fullscreen = on;
    if (window_ && !opt_.headless) SDL_SetWindowFullscreen(window_, on);
}

void App::save_settings() { store_->save_settings(settings_, nullptr); }

void App::set_language(const std::string& code) {
    settings_.language = code;
    std::string locale;
    int n = 0;
    if (SDL_Locale** locs = SDL_GetPreferredLocales(&n)) {
        if (n > 0 && locs[0]->language) locale = std::string(locs[0]->language) + (locs[0]->country ? std::string("-") + locs[0]->country : "");
        SDL_free(locs);
    }
    std::string lang = i18n::Strings::resolve(code, locale);
    strings_.clear();
    if (lang != "en") strings_.load_language(opt_.extracted + "/strings", lang);
}

void App::play_sound(const std::string& name, bool loop, int volume_arg) {
    sound_->play_sound(name, loop, volume_arg < 0 ? -1.0f : static_cast<float>(volume_arg));
}

void App::update_layout() {
    SDL_GetCurrentRenderOutputSize(renderer_, &out_w_, &out_h_);
    sbso::Insets insets;  // notch / rounded corners / home bar, in physical pixels
    int ww = 0, wh = 0;
    SDL_Rect sa;
    if (SDL_GetWindowSize(window_, &ww, &wh) && ww > 0 && wh > 0 && SDL_GetWindowSafeArea(window_, &sa)) {
        float sx = static_cast<float>(out_w_) / ww, sy = static_cast<float>(out_h_) / wh;
        insets.left = sa.x * sx;
        insets.top = sa.y * sy;
        insets.right = (ww - sa.x - sa.w) * sx;
        insets.bottom = (wh - sa.y - sa.h) * sy;
    }
    layout_ = sbso::compute_layout(out_w_, out_h_, insets);
    float s = layout_.scale;
    view_ = engine::Matrix{s, 0, 0, s, (out_w_ - sbso::kDesignW * s) * 0.5f, (out_h_ - sbso::kDesignH * s) * 0.5f};
}

// The scene works between 4:3 and 2.2:1; outside that range there are black bars. macOS 26 traps inside AppKit with SDL's live aspect limit
// (see the constructor), so instead the window snaps to the nearest valid shape once a resize has settled and the mouse is released.
void App::snap_window_aspect() {
    if (!window_ || opt_.headless || kMobile) return;
    if (SDL_GetWindowFlags(window_) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED)) return;
    if (SDL_GetGlobalMouseState(nullptr, nullptr) != 0) { resized_at_ns_ = SDL_GetTicksNS(); return; }  // still dragging
    int w = 0, h = 0;
    if (!SDL_GetWindowSize(window_, &w, &h) || w <= 0 || h <= 0) return;
    const float lo = 4.0f / 3.0f, hi = static_cast<float>(sbso::kMaxLogicalW) / sbso::kDesignH, a = static_cast<float>(w) / h;
    int nw = w, nh = h;
    if (a > hi + 0.01f) nw = static_cast<int>(std::lround(h * hi));       // too wide: bring the right edge in
    else if (a < lo - 0.01f) nh = static_cast<int>(std::lround(w / lo));  // too tall: bring the bottom edge up
    if (nw == w && nh == h) return;
    std::fprintf(stderr, "window: %dx%d -> %dx%d (aspect limits)\n", w, h, nw, nh);
    SDL_SetWindowSize(window_, nw, nh);
}

engine::Point App::to_logical(float x, float y) const {
    engine::Matrix inv = view_.inverse();
    return inv.apply({x, y});
}

bool App::render_frame() {
    update_layout();
    float s = layout_.scale;
    float zoom = screen_ ? std::max(1.0f, screen_->content_zoom()) : 1.0f;
    float asset_scale = std::min(4.0f, std::max(1.0f, std::round(s * zoom * 4.0f) / 4.0f));
    bool lanczos = settings_.graphics_filter != "original";
    // Resampling every bitmap is expensive (and memory hungry at big sizes): while the window is being dragged keep the textures
    // that exist and only rebuild them once the size has settled.
    if (out_w_ != last_out_w_ || out_h_ != last_out_h_) { last_out_w_ = out_w_; last_out_h_ = out_h_; stable_frames_ = 0; }
    else if (stable_frames_ < 1000) ++stable_frames_;
    if (!asset_filter_set_ || stable_frames_ >= 12) {
        gfx_->set_asset_filter(lanczos ? asset_scale : 1.0f, lanczos ? sbso::Filter::Lanczos3 : sbso::Filter::Nearest);
        asset_filter_set_ = true;
    }
    // 4:3 screens on a wider window: draw the frame offscreen, then stretch its edge columns into the side margins (darkened)
    float bx0 = view_.tx, bx1 = view_.tx + sbso::kDesignW * s;
    bool fill_margins = layout_.logical_w > sbso::kDesignW && screen_ && !screen_->uses_wide_layout() && bx0 >= 2.0f;
    if (fill_margins) {
        if (!frame_tex_ || frame_tex_w_ != out_w_ || frame_tex_h_ != out_h_) {
            if (frame_tex_) SDL_DestroyTexture(frame_tex_);
            frame_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, out_w_, out_h_);
            frame_tex_w_ = out_w_;
            frame_tex_h_ = out_h_;
        }
        if (frame_tex_) SDL_SetRenderTarget(renderer_, frame_tex_);
        else fill_margins = false;
    }
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    std::vector<engine::DrawCmd> cmds;
    if (screen_) screen_->draw(*this, cmds);
    for (auto& o : overlays_) o->draw(*this, cmds);
    if (cursor_inside_ && !opt_.headless && cursor_img_ >= 0) {  // GameCursor: the OS cursor is replaced by the finger bitmap
        engine::DrawCmd d;
        if (engine::image_cmd(cursor_lib_, cursor_img_, engine::Matrix{1, 0, 0, 1, cursor_pos_.x - 7, cursor_pos_.y - 3}, &d)) cmds.push_back(d);
    }
    gfx_->draw(cmds, view_, out_w_, out_h_);
    float sx0 = view_.tx - (layout_.logical_w - sbso::kDesignW) * 0.5f * s, sx1 = out_w_ - sx0;  // extended scene bounds
    if (!fill_margins && sx0 >= 1.0f) {  // wider than the scene can extend: black side bars over whatever spilled out
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_FRect bars[2] = {{0, 0, std::floor(sx0), static_cast<float>(out_h_)}, {std::ceil(sx1), 0, out_w_ - std::ceil(sx1), static_cast<float>(out_h_)}};
        SDL_RenderFillRects(renderer_, bars, 2);
    }
    if (fill_margins) {
        SDL_SetRenderTarget(renderer_, nullptr);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        // ambient fill: a tiny downscaled copy of the frame (linear filtering blurs it). Each margin continues the frame's edge
        // colour, so there is no seam, and darkens gradually away from the frame.
        float lx = std::ceil(bx0), rx = std::floor(bx1), h = static_cast<float>(out_h_);
        int bw = std::max(4, out_w_ / 80), bh = 6;  // few rows: the margins only carry the broad colour of the frame edge
        if (!blur_tex_ || blur_tex_w_ != bw || blur_tex_h_ != bh) {
            if (blur_tex_) SDL_DestroyTexture(blur_tex_);
            blur_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, bw, bh);
            blur_tex_w_ = bw;
            blur_tex_h_ = bh;
            if (blur_tex_) SDL_SetTextureScaleMode(blur_tex_, SDL_SCALEMODE_LINEAR);
        }
        SDL_SetTextureScaleMode(frame_tex_, SDL_SCALEMODE_LINEAR);
        float mw = static_cast<float>(out_w_) - rx;  // right margin width (left one is lx)
        if (blur_tex_) {
            SDL_SetRenderTarget(renderer_, blur_tex_);
            SDL_FRect design{lx, 0, rx - lx, h};  // only the 4:3 area, not the black margins
            SDL_RenderTexture(renderer_, frame_tex_, &design, nullptr);
            SDL_SetRenderTarget(renderer_, nullptr);
            // each margin stretches the blurred edge column next to it (a wider strip drags inner art such as panels out as bands)
            SDL_FRect src_l{0, 0, 0.5f, static_cast<float>(bh)}, dst_l{0, 0, lx, h};
            SDL_FRect src_r{bw - 0.5f, 0, 0.5f, static_cast<float>(bh)}, dst_r{rx, 0, mw, h};
            SDL_RenderTexture(renderer_, blur_tex_, &src_l, &dst_l);
            SDL_RenderTexture(renderer_, blur_tex_, &src_r, &dst_r);
        }
        // shade: 40 at the seam rising to 150 at a quarter of the frame width away
        float ramp = (rx - lx) * 0.25f;
        auto shade = [&](float seam, float dir, float width) {
            float far = seam + dir * std::min(ramp, width), edge = seam + dir * width;
            SDL_FColor c0{0, 0, 0, 40 / 255.0f}, c1{0, 0, 0, 150 / 255.0f};
            SDL_Vertex v[8] = {{{seam, 0}, c0, {0, 0}}, {{far, 0}, c1, {0, 0}}, {{far, h}, c1, {0, 0}}, {{seam, h}, c0, {0, 0}},
                               {{far, 0}, c1, {0, 0}}, {{edge, 0}, c1, {0, 0}}, {{edge, h}, c1, {0, 0}}, {{far, h}, c1, {0, 0}}};
            int idx[12] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
            SDL_RenderGeometry(renderer_, nullptr, v, 8, idx, 12);
        };
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        shade(lx, -1.0f, lx);
        shade(rx, 1.0f, mw);
        SDL_FRect centre{lx, 0, rx - lx, static_cast<float>(out_h_)};
        SDL_RenderTexture(renderer_, frame_tex_, &centre, &centre);
    }
    return true;
}

bool App::screenshot(const std::string& path) {
    int w, h;
    std::vector<unsigned char> px;
    if (!gfx_->read_pixels(&w, &h, &px)) return false;
    return stbi_write_png(path.c_str(), w, h, 4, px.data(), w * 4) != 0;
}

void App::inject_pointer(engine::Point p, bool down, bool up) {
    Screen* s = overlays_.empty() ? screen_.get() : overlays_.back().get();
    if (!s) return;
    s->pointer_move(*this, p);
    if (down) s->pointer_down(*this, p);
    if (up) s->pointer_up(*this, p);
}

int App::run(std::unique_ptr<Screen> first, int max_ticks) {
    screen_ = std::move(first);
    uint64_t last = SDL_GetTicksNS();
    resized_at_ns_ = last;  // a window created outside the limits snaps too
    double acc = 0;
    int ticks = 0;
    while (!quit_) {
        if (pending_) { screen_ = std::move(pending_); overlays_.clear(); }
        if (close_overlay_) { if (!overlays_.empty()) overlays_.pop_back(); close_overlay_ = false; }
        if (pending_overlay_) {
            // whatever is underneath stops being hovered while the overlay has the pointer
            if (Screen* below = overlays_.empty() ? screen_.get() : overlays_.back().get()) below->pointer_move(*this, {-1000.0f, -1000.0f});
            overlays_.push_back(std::move(pending_overlay_));
        }
        Screen* top = overlays_.empty() ? screen_.get() : overlays_.back().get();
        SDL_Event e;
        while (!opt_.headless && SDL_PollEvent(&e)) {
            SDL_ConvertEventToRenderCoordinates(renderer_, &e);
            switch (e.type) {
                case SDL_EVENT_QUIT: suspend(); quit_ = true; break;
                case SDL_EVENT_WILL_ENTER_BACKGROUND: case SDL_EVENT_TERMINATING: suspend(); break;
                case SDL_EVENT_MOUSE_MOTION: cursor_pos_ = to_logical(e.motion.x, e.motion.y); cursor_inside_ = true; if (top) top->pointer_move(*this, cursor_pos_); break;
                case SDL_EVENT_WINDOW_MOUSE_LEAVE: cursor_inside_ = false; break;
                case SDL_EVENT_WINDOW_RESIZED: resized_at_ns_ = SDL_GetTicksNS(); break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN: if (top && e.button.button == SDL_BUTTON_LEFT) top->pointer_down(*this, to_logical(e.button.x, e.button.y)); break;
                case SDL_EVENT_MOUSE_BUTTON_UP: if (top && e.button.button == SDL_BUTTON_LEFT) top->pointer_up(*this, to_logical(e.button.x, e.button.y)); break;
                // Touch: only the first finger drives the pointer; hover is emulated (move before press) and cleared on release.
                // finger x/y are already in render pixels here: SDL_ConvertEventToRenderCoordinates (above) converts them from 0..1
                case SDL_EVENT_FINGER_DOWN:
                    cursor_inside_ = false;
                    if (top && !finger_active_) {
                        finger_active_ = true;
                        finger_id_ = e.tfinger.fingerID;
                        auto p = to_logical(e.tfinger.x, e.tfinger.y);
                        top->pointer_move(*this, p);
                        top->pointer_down(*this, p);
                    }
                    break;
                case SDL_EVENT_FINGER_MOTION:
                    if (top && finger_active_ && e.tfinger.fingerID == finger_id_) top->pointer_move(*this, to_logical(e.tfinger.x, e.tfinger.y));
                    break;
                case SDL_EVENT_FINGER_UP:
                    if (finger_active_ && e.tfinger.fingerID == finger_id_) {
                        finger_active_ = false;
                        if (top) {
                            top->pointer_up(*this, to_logical(e.tfinger.x, e.tfinger.y));
                            top->pointer_move(*this, {-1000, -1000});  // no hover without a pointer
                        }
                    }
                    break;
                case SDL_EVENT_FINGER_CANCELED:  // the system took the touch (gesture, notification): release without a click
                    if (finger_active_ && e.tfinger.fingerID == finger_id_) {
                        finger_active_ = false;
                        if (top) top->pointer_move(*this, {-1000, -1000});
                        if (top) top->pointer_up(*this, {-1000, -1000});
                    }
                    break;
                case SDL_EVENT_TEXT_INPUT: if (top) top->text_input(*this, e.text.text); break;
                case SDL_EVENT_KEY_DOWN: if (top) top->key_down(*this, static_cast<int>(e.key.key)); break;
                default: break;
            }
        }
        if (!screen_) break;
        if (opt_.headless) {
            if (max_ticks >= 0 && ticks >= max_ticks) break;
            if (tick_hook_) tick_hook_(*this, ticks);
            (overlays_.empty() ? screen_.get() : overlays_.back().get())->tick(*this);
            ++ticks;
            continue;
        }
        uint64_t now = SDL_GetTicksNS();
        acc += (now - last) * 1e-9;
        last = now;
        double step = 1.0 / screen_->tick_rate();
        if (acc > 0.25) acc = 0.25;  // do not spiral after a stall
        while (acc >= step && !pending_ && !pending_overlay_ && !close_overlay_) {
            (overlays_.empty() ? screen_.get() : overlays_.back().get())->tick(*this);
            acc -= step;
            ++ticks;
        }
        if (resized_at_ns_ && now - resized_at_ns_ > 300000000ull) { resized_at_ns_ = 0; snap_window_aspect(); }
        render_frame();
        SDL_RenderPresent(renderer_);
    }
    if (opt_.headless) render_frame();
    return 0;
}

}  // namespace sbso::app
