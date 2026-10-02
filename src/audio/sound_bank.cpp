#include "sound_bank.h"
#include "core/vfs.h"

#include <cstring>
#include <fstream>

#include "json.hpp"

#define MINIMP3_FLOAT_OUTPUT  // decode to float like ffmpeg's mp3float decoder (no 16-bit rounding before resampling)
#include "minimp3_ex.h"

#include <cmath>
#include <mutex>
#include <numeric>

namespace sbso::audio {

namespace {
// Interleaved 16-bit PCM -> mono float at kSampleRate (stereo averaged). (WAV files of very old extractions.)
void pcm16_to_sound(const short* pcm, size_t frames, int channels, int rate, Sound* out) {
    std::vector<float> mono(frames);
    for (size_t i = 0; i < frames; ++i) {
        float acc = 0;
        for (int c = 0; c < channels; ++c) acc += pcm[i * channels + c] / 32768.0f;
        mono[i] = acc / channels;
    }
    if (rate != kSampleRate && frames > 0) {  // linear interpolation fallback; the extractor already writes 48 kHz
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
}
}  // namespace

namespace {
// Zeroth-order modified Bessel function of the first kind (Kaiser window).
double bessel_i0(double x) {
    double sum = 1, term = 1;
    for (int k = 1; k < 64; ++k) {
        term *= (x / (2 * k)) * (x / (2 * k));
        sum += term;
        if (term < sum * 1e-17) break;
    }
    return sum;
}

// Polyphase windowed-sinc resampling, a replica of libswresample (FFmpeg 8.1, resample.c) as the extractor used to run it
// (aresample=resampler=swr:filter_size=64:phase_shift=15:linear_interp=0, float processing, exact rational phase count): same filter
// length, centre, Kaiser window (beta 9) and normalisation (every phase divided by the DC gain of phase 0), same phase stepping and the
// same float accumulation, so the output matches what ffmpeg wrote to the old WAV files.
struct SincKernel {
    static constexpr double kCutoff = 0.97, kBeta = 9.0, kPi = 3.14159265358979323846;
    static constexpr int kFilterSize = 64, kPhaseShift = 15;
    int taps = 0, center = 0, phases = 0;
    long in_step = 0;          // phases advanced per output sample
    std::vector<float> table;  // phases x taps
    explicit SincKernel(int in_rate) {
        const int g = std::gcd(in_rate, kSampleRate);
        const double factor = std::min(kSampleRate * kCutoff / in_rate, 1.0);
        taps = std::max(static_cast<int>(std::ceil(kFilterSize / factor)), 1);
        if (taps > 1) taps = (taps + 1) & ~1;
        center = (taps - 1) / 2;
        phases = 1 << kPhaseShift;
        if (kSampleRate / g <= phases) phases = kSampleRate / g;  // exact_rational
        in_step = static_cast<long>(in_rate) * phases / kSampleRate;
        table.resize(static_cast<size_t>(phases) * taps);
        std::vector<double> tab(static_cast<size_t>(taps));
        double norm = 0;
        for (int ph = 0; ph < phases; ++ph) {
            for (int i = 0; i < taps; ++i) {
                const double x = kPi * ((i - center) - static_cast<double>(ph) / phases) * factor;
                double y = x == 0 ? 1.0 : std::sin(x) / x;
                const double w = 2.0 * x / (factor * taps * kPi);
                y *= bessel_i0(kBeta * std::sqrt(std::max(1 - w * w, 0.0)));
                tab[static_cast<size_t>(i)] = y;
                if (ph == 0) norm += y;
            }
            for (int i = 0; i < taps; ++i) table[static_cast<size_t>(ph) * taps + i] = static_cast<float>(tab[static_cast<size_t>(i)] / norm);
        }
    }
};

const SincKernel& kernel_for(int in_rate) {  // a handful of source rates (11025, 22050, 44100): built once each
    static std::mutex m;
    static std::map<int, std::unique_ptr<SincKernel>> kernels;
    std::lock_guard<std::mutex> lock(m);
    auto& k = kernels[in_rate];
    if (!k) k = std::make_unique<SincKernel>(in_rate);
    return *k;
}
}  // namespace

std::vector<float> resample_to_output(const std::vector<float>& in, int src_rate) {
    if (src_rate == kSampleRate || in.empty() || src_rate <= 0) return in;
    const SincKernel& K = kernel_for(src_rate);
    if (static_cast<long>(src_rate) * K.phases % kSampleRate != 0) {  // not an exact ratio: plain linear interpolation fallback
        std::vector<float> r(static_cast<size_t>(static_cast<double>(in.size()) * kSampleRate / src_rate));
        for (size_t i = 0; i < r.size(); ++i) {
            double p = static_cast<double>(i) * src_rate / kSampleRate;
            size_t i0 = std::min(static_cast<size_t>(p), in.size() - 1), i1 = std::min(i0 + 1, in.size() - 1);
            r[i] = in[i0] + (in[i1] - in[i0]) * static_cast<float>(p - std::floor(p));
        }
        return r;
    }
    const size_t n_out = static_cast<size_t>((static_cast<std::uint64_t>(in.size()) * kSampleRate + src_rate - 1) / src_rate);
    std::vector<float> out(n_out);
    const long n_in = static_cast<long>(in.size());
    // before the first sample: silence; after the last one swr's flush mirrors the last (min(n, taps) + 1) / 2 samples, then silence
    const long reflection = (std::min<long>(n_in, K.taps) + 1) / 2;
    auto sample = [&](long i) -> float {
        if (i < 0) return 0.0f;
        if (i < n_in) return in[static_cast<size_t>(i)];
        const long r = i - n_in;
        return r < reflection ? in[static_cast<size_t>(n_in - 1 - r)] : 0.0f;
    };
    for (size_t n = 0; n < n_out; ++n) {
        const long index = static_cast<long>(n) * K.in_step;  // in phases; starts centred on input sample 0
        const long first = index / K.phases - K.center;
        const float* f = &K.table[static_cast<size_t>(index % K.phases) * K.taps];
        float val = 0, val2 = 0;  // even / odd taps summed separately in float, like swr
        for (int i = 0; i + 1 < K.taps; i += 2) {
            val += sample(first + i) * f[i];
            val2 += sample(first + i + 1) * f[i + 1];
        }
        out[n] = val + val2;
    }
    return out;
}

bool load_mp3(const std::string& path, Sound* out, std::string* err) {
    std::vector<unsigned char> d;
    if (!vfs::read(path, &d)) { if (err) *err = "cannot open " + path; return false; }
    mp3dec_t dec;
    mp3dec_file_info_t info{};
    if (mp3dec_load_buf(&dec, d.data(), d.size(), &info, nullptr, nullptr) || !info.buffer || info.samples == 0 || info.channels < 1) {
        if (info.buffer) free(info.buffer);
        if (err) *err = "cannot decode " + path;
        return false;
    }
    const size_t frames = info.samples / static_cast<size_t>(info.channels);
    std::vector<float> mono(frames);
    for (size_t i = 0; i < frames; ++i) {
        float acc = 0;
        for (int c = 0; c < info.channels; ++c) acc += info.buffer[i * info.channels + c];  // float output (MINIMP3_FLOAT_OUTPUT)
        mono[i] = acc / info.channels;
    }
    free(info.buffer);
    out->samples = resample_to_output(mono, info.hz);
    return true;
}

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
    std::vector<short> samples(frames * channels);
    for (size_t i = 0; i < samples.size(); ++i) samples[i] = static_cast<short>(pcm[i * 2] | pcm[i * 2 + 1] << 8);
    pcm16_to_sound(samples.data(), frames, channels, rate, out);
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
            // the game's MP3 (current extractions) or the WAV very old extractions wrote
            const std::string rel = "/" + std::string(lib) + "/" + it.key();
            Entry e;
            for (const std::string& p : {root + "/sounds" + rel + ".mp3", root + "/sounds_wav" + rel + ".wav"})
                if (vfs::exists(p)) { e.path = p; break; }
            if (e.path.empty()) continue;
            sounds_[it.value().get<std::string>()] = std::move(e);
            ++n;
        }
    }
    return n;
}

std::shared_ptr<const Sound> SoundBank::get(const std::string& name) const {
    auto it = sounds_.find(name);
    if (it == sounds_.end()) return nullptr;
    const Entry& e = it->second;
    if (!e.sound && !e.failed) {
        auto s = std::make_shared<Sound>();
        const std::string& p = e.path;
        auto ends = [&](const char* ext) { size_t n = std::strlen(ext); return p.size() > n && p.compare(p.size() - n, n, ext) == 0; };
        bool ok = ends(".mp3") ? load_mp3(p, s.get(), nullptr) : load_wav(p, s.get(), nullptr);
        if (ok) e.sound = std::move(s);
        else e.failed = true;
    }
    return e.sound;
}

}  // namespace sbso::audio
