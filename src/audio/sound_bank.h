// WAV loading and the named sound library (names come from the SWF symbol table).
#pragma once
#include <map>
#include <memory>
#include <string>

#include "mixer.h"

namespace sbso::audio {

// Reads 16-bit PCM mono/stereo WAV (stereo is averaged); resamples to 48 kHz only if the file differs.
bool load_wav(const std::string& path, Sound* out, std::string* err);

class SoundBank {
public:
    // `root` is the extractor output; loads sound_library and music_library (names -> wav files).
    int load(const std::string& root);
    std::shared_ptr<const Sound> get(const std::string& name) const;
    size_t size() const { return sounds_.size(); }

private:
    std::map<std::string, std::shared_ptr<const Sound>> sounds_;
};

}  // namespace sbso::audio
