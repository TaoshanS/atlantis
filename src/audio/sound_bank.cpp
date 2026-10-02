#include "sound_bank.h"
#include "core/vfs.h"

#include <cstring>
#include <fstream>

#include "json.hpp"

namespace sbso::audio {

bool load_wav(const std::string& path, Sound* out, std::string* err) {
    std::vector<unsigned char> d;
    if (!vfs::read(path, &d)) { if (err) *err = "cannot open " + path; return false; }
    if (d.size() < 44 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) { if (err) *err = "not a WAV: " + path; return false; }
    int channels = 1, rate = kSampleRate, bits = 16;
    size_t pos = 12;
    const unsigned char* pcm = nullptr;
    size_t pcm_len = 0;
    while (pos + 8 <= d.size()) {
        std::uint32_t len = d[pos + 4] | d[pos + 5] << 8 | d[pos + 6] << 16 | static_cast<std::uint32_t>(d[pos + 7]) << 24;
        if (!std::memcmp(&d[pos], "fmt ", 4) && pos + 8 + 16 <= d.size()) {
            int fmt = d[pos + 8] | d[pos + 9] << 8;
            channels = d[pos + 10] | d[pos + 11] << 8;
            rate = d[pos + 12] | d[pos + 13] << 8 | d[pos + 14] << 16 | d[pos + 15] << 24;
            bits = d[pos + 22] | d[pos + 23] << 8;
            if (fmt != 1) { if (err) *err = "unsupported WAV format"; return false; }
        } else if (!std::memcmp(&d[pos], "data", 4)) {
            pcm = &d[pos + 8];
            pcm_len = std::min<size_t>(len, d.size() - pos - 8);
            break;
        }
        pos += 8 + len + (len & 1);
    }
    if (!pcm || bits != 16 || channels < 1 || channels > 2) { if (err) *err = "unsupported WAV layout: " + path; return false; }
    size_t frames = pcm_len / (2 * channels);
    std::vector<float> mono(frames);
    for (size_t i = 0; i < frames; ++i) {
        float acc = 0;
        for (int c = 0; c < channels; ++c) {
            short v = static_cast<short>(pcm[(i * channels + c) * 2] | pcm[(i * channels + c) * 2 + 1] << 8);
            acc += v / 32768.0f;
        }
        mono[i] = acc / channels;
    }
    if (rate != kSampleRate) {  // linear interpolation fallback; the extractor already writes 48 kHz
        std::vector<float> r(static_cast<size_t>(static_cast<double>(frames) * kSampleRate / rate));
        for (size_t i = 0; i < r.size(); ++i) {
            double p = static_cast<double>(i) * rate / kSampleRate;
            size_t i0 = static_cast<size_t>(p);
            float fr = static_cast<float>(p - i0);
            float a = mono[std::min(i0, frames - 1)], b = mono[std::min(i0 + 1, frames - 1)];
            r[i] = a + (b - a) * fr;
        }
        mono.swap(r);
    }
    out->samples = std::move(mono);
    return true;
}

int SoundBank::load(const std::string& root) {
    int n = 0;
    for (const char* lib : {"sound_library", "music_library"}) {
        std::string text;
        if (!vfs::read_text(root + "/characters/" + lib + ".json", &text)) continue;
        nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.contains("symbols")) continue;
        for (auto it = j["symbols"].begin(); it != j["symbols"].end(); ++it) {
            if (it.key() == "0") continue;
            auto s = std::make_shared<Sound>();
            if (!load_wav(root + "/sounds_wav/" + lib + "/" + it.key() + ".wav", s.get(), nullptr)) continue;
            sounds_[it.value().get<std::string>()] = std::move(s);
            ++n;
        }
    }
    return n;
}

std::shared_ptr<const Sound> SoundBank::get(const std::string& name) const {
    auto it = sounds_.find(name);
    return it == sounds_.end() ? nullptr : it->second;
}

}  // namespace sbso::audio
