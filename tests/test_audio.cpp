#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "audio/mixer.h"
#include "audio/sound_bank.h"
#include "audio/sound_manager.h"

using namespace sbso::audio;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static std::shared_ptr<Sound> tone(size_t n, float v) {
    auto s = std::make_shared<Sound>();
    s->samples.assign(n, v);
    return s;
}

static void write_wav(const std::string& path, int rate, int channels, const std::vector<short>& pcm) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](unsigned v) { for (int i = 0; i < 4; ++i) f.put(static_cast<char>(v >> (8 * i))); };
    auto u16 = [&](unsigned v) { for (int i = 0; i < 2; ++i) f.put(static_cast<char>(v >> (8 * i))); };
    f.write("RIFF", 4); u32(36 + static_cast<unsigned>(pcm.size()) * 2); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(channels); u32(rate); u32(rate * channels * 2); u16(channels * 2); u16(16);
    f.write("data", 4); u32(static_cast<unsigned>(pcm.size()) * 2);
    for (short v : pcm) u16(static_cast<unsigned short>(v));
}

int main() {
    // One-shot playback ends and frees the channel; ids are reused.
    {
        Mixer m;
        int a = m.play(tone(100, 0.5f), false);
        CHECK(a == 0 && m.playing(a));
        std::vector<float> out(2 * 64, 0.0f);
        m.mix(out.data(), 64);
        CHECK(m.playing(a));
        m.mix(out.data(), 64);  // 128 frames requested > 100 available
        CHECK(!m.playing(a));
        CHECK(m.play(tone(10, 0.1f), false) == 0);
    }
    // Pan law, volume buses and clamping.
    {
        Mixer m;
        m.set_effects_volume(0.5f);
        m.play(tone(8, 1.0f), false, 1.0f, -1.0f);  // hard left
        std::vector<float> out(16, 0.0f);
        m.mix(out.data(), 8);
        CHECK(std::fabs(out[0] - 0.5f) < 1e-4 && std::fabs(out[1]) < 1e-4);
        Mixer loud;
        loud.play(tone(4, 1.0f), false);
        loud.play(tone(4, 1.0f), false);
        std::vector<float> o2(8, 0.0f);
        loud.mix(o2.data(), 4);
        CHECK(o2[0] <= 1.0f && o2[0] > 0.9f);
        Mixer music;
        music.set_music_volume(0.0f);
        music.play(tone(4, 1.0f), false, 1.0f, 0.0f, true);
        std::vector<float> o3(8, 0.0f);
        music.mix(o3.data(), 4);
        CHECK(o3[0] == 0.0f);
    }
    // Looping and fade-out with stop.
    {
        Mixer m;
        int c = m.play(tone(10, 1.0f), true, 1.0f);
        std::vector<float> out(2 * 1000, 0.0f);
        m.mix(out.data(), 1000);
        CHECK(m.playing(c));
        m.fade(c, 0.0f, 0.01f, true);  // 480 frames
        std::fill(out.begin(), out.end(), 0.0f);
        m.mix(out.data(), 1000);
        CHECK(!m.playing(c));
        CHECK(out[0] > out[2 * 300] && out[2 * 900] == 0.0f);
    }
    // WAV reader: stereo averaging and resampling from another rate.
    {
        fs::path dir = fs::path(TEST_TMP_DIR) / "audio_test";
        fs::create_directories(dir);
        write_wav((dir / "st.wav").string(), 48000, 2, {16384, -16384, 8192, 8192});
        Sound s; std::string err;
        CHECK(load_wav((dir / "st.wav").string(), &s, &err) && s.samples.size() == 2);
        CHECK(std::fabs(s.samples[0]) < 1e-4 && std::fabs(s.samples[1] - 0.25f) < 1e-3);
        write_wav((dir / "lo.wav").string(), 24000, 1, std::vector<short>(100, 1000));
        CHECK(load_wav((dir / "lo.wav").string(), &s, &err) && s.samples.size() == 200);
        CHECK(!load_wav((dir / "missing.wav").string(), &s, &err));
        fs::remove_all(dir);
    }
    // SoundManager semantics with an in-memory bank.
    {
        struct Bank : SoundBank {};
        Mixer m;
        SoundBank bank;
        fs::path dir = fs::path(TEST_TMP_DIR) / "audio_test2" / "sounds_wav" / "sound_library";
        fs::create_directories(dir);
        write_wav((dir / "1.wav").string(), 48000, 1, std::vector<short>(4800, 16384));
        fs::create_directories(fs::path(TEST_TMP_DIR) / "audio_test2" / "characters");
        { std::ofstream j(fs::path(TEST_TMP_DIR) / "audio_test2" / "characters" / "sound_library.json"); j << "{\"symbols\":{\"1\":\"beep\"}}"; }
        CHECK(bank.load((fs::path(TEST_TMP_DIR) / "audio_test2").string()) == 1);
        SoundManager sm(&m, &bank, 30.0f);
        sm.set_effects_volume(0.5f);
        int ch = sm.play_sound("beep", false, -1.0f);          // -1: effects volume
        CHECK(ch >= 0 && sm.play_sound("nope") == -1);
        std::vector<float> out(2 * 10, 0.0f);
        m.mix(out.data(), 10);
        CHECK(std::fabs(out[0] / std::cos(3.14159265f * 0.25f) - 0.25f) < 1e-3);  // 0.5 sample * 0.5 volume
        sm.set_all_music_volume(0.5f);
        int mc = sm.play_music("beep", true, 1.0f);
        CHECK(sm.music_playing("beep") && sm.play_music("beep", true, 0.5f) == mc);  // same channel reused
        sm.fade_music_out("beep", 3);                        // 0.1 s at 30 fps
        std::vector<float> big(2 * 6000, 0.0f);
        m.mix(big.data(), 6000);
        CHECK(!sm.music_playing("beep"));
        fs::remove_all(fs::path(TEST_TMP_DIR) / "audio_test2");
    }
    // The real library, when the extractor output exists.
    {
        const char* env = std::getenv("SBSO_EXTRACTED");
        std::string root = env ? env : EXTRACTED_DIR;
        if (fs::exists(root + "/sounds_wav")) {
            SoundBank bank;
            int n = bank.load(root);
            std::printf("sound bank: %d sounds\n", n);
            CHECK(n >= 150);
            CHECK(bank.get("button_play") != nullptr);
        } else {
            std::printf("skipped sound bank (no extractor output)\n");
        }
    }
    std::printf(failures ? "%d FAILED\n" : "all audio tests passed\n", failures);
    return failures ? 1 : 0;
}
