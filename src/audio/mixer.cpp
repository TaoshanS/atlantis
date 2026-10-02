#include "mixer.h"

#include <algorithm>
#include <cmath>

namespace sbso::audio {

int Mixer::play(std::shared_ptr<const Sound> s, bool loop, float volume, float pan, bool music) {
    std::lock_guard<std::mutex> g(m_);
    if (!s || s->samples.empty()) return -1;
    int idx = -1;
    for (size_t i = 0; i < ch_.size(); ++i)
        if (!ch_[i].active) { idx = static_cast<int>(i); break; }
    if (idx < 0) { ch_.emplace_back(); idx = static_cast<int>(ch_.size()) - 1; }
    Channel& c = ch_[idx];
    c = Channel{};
    c.sound = std::move(s);
    c.loop = loop;
    c.volume = volume;
    c.pan = std::max(-1.0f, std::min(1.0f, pan));
    c.music = music;
    c.active = true;
    return idx;
}

void Mixer::stop(int ch) {
    std::lock_guard<std::mutex> g(m_);
    if (ch >= 0 && ch < static_cast<int>(ch_.size())) { ch_[ch].active = false; ch_[ch].sound.reset(); }
}

void Mixer::stop_all() {
    std::lock_guard<std::mutex> g(m_);
    for (auto& c : ch_) { c.active = false; c.sound.reset(); }
}

bool Mixer::playing(int ch) const {
    std::lock_guard<std::mutex> g(m_);
    return ch >= 0 && ch < static_cast<int>(ch_.size()) && ch_[ch].active;
}

void Mixer::set_volume(int ch, float v) {
    std::lock_guard<std::mutex> g(m_);
    if (ch >= 0 && ch < static_cast<int>(ch_.size()) && ch_[ch].active) ch_[ch].volume = v;
}

void Mixer::set_pan(int ch, float p) {
    std::lock_guard<std::mutex> g(m_);
    if (ch >= 0 && ch < static_cast<int>(ch_.size()) && ch_[ch].active) ch_[ch].pan = std::max(-1.0f, std::min(1.0f, p));
}

void Mixer::fade(int ch, float target, float seconds, bool stop_after) {
    std::lock_guard<std::mutex> g(m_);
    if (!(ch >= 0 && ch < static_cast<int>(ch_.size()) && ch_[ch].active)) return;
    Channel& c = ch_[ch];
    int frames = std::max(1, static_cast<int>(std::lround(seconds * kSampleRate)));
    c.fade_target = target;
    c.fade_step = (target - c.volume) / frames;
    c.fading = true;
    c.stop_after_fade = stop_after;
}

int Mixer::active_channels() const {
    std::lock_guard<std::mutex> g(m_);
    int n = 0;
    for (const auto& c : ch_) n += c.active;
    return n;
}

void Mixer::mix(float* out, int frames) {
    std::lock_guard<std::mutex> g(m_);
    for (Channel& c : ch_) {
        if (!c.active) continue;
        const std::vector<float>& s = c.sound->samples;
        const float bus = c.music ? music_vol_ : fx_vol_;
        // Equal-power pan.
        const float angle = (c.pan + 1.0f) * 0.25f * 3.14159265f;
        const float gl = std::cos(angle), gr = std::sin(angle);
        for (int i = 0; i < frames; ++i) {
            if (c.pos >= s.size()) {
                if (c.loop) c.pos = 0;
                else { c.active = false; c.sound.reset(); break; }
            }
            if (c.fading) {
                c.volume += c.fade_step;
                if ((c.fade_step >= 0 && c.volume >= c.fade_target) || (c.fade_step < 0 && c.volume <= c.fade_target)) {
                    c.volume = c.fade_target;
                    c.fading = false;
                    if (c.stop_after_fade) { c.active = false; c.sound.reset(); break; }
                }
            }
            float v = s[c.pos++] * c.volume * bus;
            out[2 * i] += v * gl;
            out[2 * i + 1] += v * gr;
        }
    }
    for (int i = 0; i < frames * 2; ++i) out[i] = std::max(-1.0f, std::min(1.0f, out[i]));
}

}  // namespace sbso::audio
