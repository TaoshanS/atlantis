// Port of tinymantis/SoundManager.as on top of Mixer/SoundBank: named sounds, channel ids,
// "volume -1 = the profile's effects volume", per-name music with global music volume and fades.
#pragma once
#include <map>
#include <string>

#include "mixer.h"
#include "sound_bank.h"

namespace sbso::audio {

class SoundManager {
public:
    SoundManager(Mixer* mixer, const SoundBank* bank, float fps = 31.0f) : mixer_(mixer), bank_(bank), fps_(fps) {}

    void set_effects_volume(float v) { fx_vol_ = v; }   // Profile.fSoundVol
    void set_frame_rate(float fps) { fps_ = fps; }

    // volume < 0 means "use the effects volume". Returns the channel id or -1 (unknown sound).
    int play_sound(const std::string& name, bool loop = false, float volume = 1.0f, float pan = 0.0f);
    void stop_sound(int ch) { mixer_->stop(ch); }

    // Music: one channel per track name; volume is multiplied by the global music volume.
    int play_music(const std::string& name, bool loop = false, float volume = 1.0f, float pan = 0.0f);
    void stop_music(const std::string& name);
    void set_music_volume(const std::string& name, float volume);
    void set_all_music_volume(float v);
    void fade_music_in(const std::string& name, int frames, bool loop = false, float final_volume = 1.0f);
    void fade_music_out(const std::string& name, int frames);
    bool music_playing(const std::string& name) const;

private:
    struct Track { int channel = -1; float volume = 1.0f; };
    Mixer* mixer_;
    const SoundBank* bank_;
    float fps_;
    float fx_vol_ = 0.6f, music_vol_ = 1.0f;
    std::map<std::string, Track> music_;
};

}  // namespace sbso::audio
