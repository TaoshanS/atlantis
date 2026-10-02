#include "sdl_audio.h"

#include <SDL3/SDL.h>

namespace sbso::audio {
namespace {

void SDLCALL callback(void* user, SDL_AudioStream* stream, int additional, int) {
    Mixer* m = static_cast<Mixer*>(user);
    int frames = additional / static_cast<int>(2 * sizeof(float));
    std::vector<float> buf(static_cast<size_t>(frames) * 2, 0.0f);
    m->mix(buf.data(), frames);
    SDL_PutAudioStreamData(stream, buf.data(), static_cast<int>(buf.size() * sizeof(float)));
}

}  // namespace

bool SdlAudioOut::start(std::string* err) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { if (err) *err = SDL_GetError(); return false; }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = kSampleRate;
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, mixer_);
    if (!stream_) { if (err) *err = SDL_GetError(); return false; }
    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void SdlAudioOut::stop() {
    if (stream_) { SDL_DestroyAudioStream(stream_); stream_ = nullptr; }
}

SdlAudioOut::~SdlAudioOut() { stop(); }

}  // namespace sbso::audio
