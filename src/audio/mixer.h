// Software mixer that mirrors tinymantis/SoundManager: numbered channels, separate effect and
// music volumes, per-channel volume/pan, looping and linear fades. Pure C++ (no SDL), so the
// mixing is unit-testable; src/audio/sdl_audio.cpp feeds it to a device.
#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sbso::audio {

constexpr int kSampleRate = 48000;

struct Sound {
    std::vector<float> samples;  // mono, kSampleRate
};

class Mixer {
public:
    // Starts a sound; returns the channel id (reused after a channel ends, like SoundManager).
    int play(std::shared_ptr<const Sound> s, bool loop, float volume = 1.0f, float pan = 0.0f, bool music = false);
    void stop(int ch);
    void stop_all();
    bool playing(int ch) const;
    void set_volume(int ch, float v);
    void set_pan(int ch, float p);
    // Linear fade of the channel volume to `target` over `seconds`; stops the channel at the end if `stop_after`.
    void fade(int ch, float target, float seconds, bool stop_after);

    void set_effects_volume(float v) { fx_vol_ = v; }
    void set_music_volume(float v) { music_vol_ = v; }
    float effects_volume() const { return fx_vol_; }
    float music_volume() const { return music_vol_; }

    // Adds `frames` stereo frames (interleaved L,R) to `out` (which the caller zeroes), clamped to [-1, 1].
    void mix(float* out, int frames);
    int active_channels() const;

private:
    struct Channel {
        std::shared_ptr<const Sound> sound;
        size_t pos = 0;
        bool loop = false, music = false, active = false;
        float volume = 1, pan = 0;
        float fade_target = 0, fade_step = 0;  // per-frame volume delta while fading
        bool fading = false, stop_after_fade = false;
    };
    mutable std::mutex m_;  // the SDL audio thread calls mix() while the game thread starts/stops sounds
    std::vector<Channel> ch_;
    float fx_vol_ = 1.0f, music_vol_ = 1.0f;
};

}  // namespace sbso::audio
