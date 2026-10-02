#include "sound_manager.h"

namespace sbso::audio {

int SoundManager::play_sound(const std::string& name, bool loop, float volume, float pan) {
    auto s = bank_->get(name);
    if (!s) return -1;  // the original throws "was not registered"; the port just stays silent
    if (volume < 0) volume = fx_vol_;
    return mixer_->play(s, loop, volume, pan, false);
}

int SoundManager::play_music(const std::string& name, bool loop, float volume, float pan) {
    auto it = music_.find(name);
    if (it != music_.end() && mixer_->playing(it->second.channel)) {
        it->second.volume = volume;
        mixer_->set_volume(it->second.channel, volume * music_vol_);
        mixer_->set_pan(it->second.channel, pan);
        return it->second.channel;
    }
    auto s = bank_->get(name);
    if (!s) return -1;
    int ch = mixer_->play(s, loop, volume * music_vol_, pan, true);
    music_[name] = Track{ch, volume};
    return ch;
}

void SoundManager::stop_music(const std::string& name) {
    auto it = music_.find(name);
    if (it == music_.end()) return;
    mixer_->stop(it->second.channel);
    music_.erase(it);
}

void SoundManager::set_music_volume(const std::string& name, float volume) {
    auto it = music_.find(name);
    if (it == music_.end()) return;
    it->second.volume = volume;
    mixer_->set_volume(it->second.channel, volume * music_vol_);
}

void SoundManager::set_all_music_volume(float v) {
    music_vol_ = v;
    for (auto& [n, t] : music_) mixer_->set_volume(t.channel, t.volume * music_vol_);
}

void SoundManager::fade_music_in(const std::string& name, int frames, bool loop, float final_volume) {
    int ch = play_music(name, loop, 0.0f);
    if (ch < 0) return;
    music_[name].volume = final_volume;
    mixer_->set_volume(ch, 0.0f);
    mixer_->fade(ch, final_volume * music_vol_, frames / fps_, false);
}

void SoundManager::fade_music_out(const std::string& name, int frames) {
    auto it = music_.find(name);
    if (it == music_.end()) return;
    mixer_->fade(it->second.channel, 0.0f, frames / fps_, true);
}

bool SoundManager::music_playing(const std::string& name) const {
    auto it = music_.find(name);
    return it != music_.end() && mixer_->playing(it->second.channel);
}

}  // namespace sbso::audio
