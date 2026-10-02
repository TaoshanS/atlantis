// Sound loading (the game's original MP3s, or WAV from very old extractions) and the named sound library (names come from the SWF
// symbol table). Every sound ends up as mono float at 48 kHz.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mixer.h"

namespace sbso::audio {

// Reads 16-bit PCM mono/stereo WAV (stereo is averaged); resamples to 48 kHz only if the file differs.
bool load_wav(const std::string& path, Sound* out, std::string* err);
// Decodes one of the game's MP3s (minimp3) and resamples it to 48 kHz with a windowed-sinc filter (the parameters of the ffmpeg
// resampler the extractor used before: 64 taps, cutoff 0.97, Kaiser beta 9).
bool load_mp3(const std::string& path, Sound* out, std::string* err);
// Band-limited resampling of mono float samples from `src_rate` to kSampleRate.
std::vector<float> resample_to_output(const std::vector<float>& in, int src_rate);

class SoundBank {
public:
    // `root` is the extractor output; indexes sound_library and music_library (names -> files). Sounds are decoded the first time they
    // are asked for (decoding and resampling everything up front would take seconds on a phone).
    int load(const std::string& root);
    std::shared_ptr<const Sound> get(const std::string& name) const;
    size_t size() const { return sounds_.size(); }

private:
    struct Entry {
        std::string path;
        mutable std::shared_ptr<const Sound> sound;  // decoded on first use
        mutable bool failed = false;
    };
    std::map<std::string, Entry> sounds_;
};

}  // namespace sbso::audio
