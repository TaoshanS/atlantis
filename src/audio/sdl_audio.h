// Plays a Mixer through the default SDL3 audio device.
#pragma once
#include "mixer.h"

struct SDL_AudioStream;

namespace sbso::audio {

class SdlAudioOut {
public:
    explicit SdlAudioOut(Mixer* mixer) : mixer_(mixer) {}
    ~SdlAudioOut();
    bool start(std::string* err);
    void stop();

private:
    Mixer* mixer_;
    SDL_AudioStream* stream_ = nullptr;
};

}  // namespace sbso::audio
