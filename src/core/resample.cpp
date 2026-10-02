#include "resample.h"

#include <algorithm>
#include <cmath>

namespace sbso {
namespace {

constexpr float kPi = 3.14159265358979f;

float lanczos3(float x) {
    x = std::fabs(x);
    if (x < 1e-6f) return 1.0f;
    if (x >= 3.0f) return 0.0f;
    float px = kPi * x;
    return 3.0f * std::sin(px) * std::sin(px / 3.0f) / (px * px);
}

struct Tap {
    int first = 0;
    std::vector<float> w;
};

// Weights for one axis; the kernel widens when downscaling so it still anti-aliases.
std::vector<Tap> make_taps(int in, int out) {
    std::vector<Tap> taps(out);
    float scale = static_cast<float>(in) / out;
    float support = 3.0f * std::max(scale, 1.0f);
    float stretch = std::max(scale, 1.0f);
    for (int o = 0; o < out; ++o) {
        float center = (o + 0.5f) * scale;
        int lo = static_cast<int>(std::floor(center - support + 0.5f));
        int hi = static_cast<int>(std::floor(center + support + 0.5f));
        Tap& t = taps[o];
        t.first = lo;
        float sum = 0;
        for (int i = lo; i < hi; ++i) {
            float w = lanczos3((i + 0.5f - center) / stretch);
            t.w.push_back(w);
            sum += w;
        }
        for (float& w : t.w) w /= sum;
    }
    return taps;
}

}  // namespace

Image resample(const Image& src, int out_w, int out_h, Filter filter) {
    Image dst;
    dst.w = out_w;
    dst.h = out_h;
    dst.rgba.assign(static_cast<size_t>(out_w) * out_h * 4, 0);
    if (src.w <= 0 || src.h <= 0 || out_w <= 0 || out_h <= 0) return dst;

    if (filter == Filter::Nearest) {
        for (int y = 0; y < out_h; ++y) {
            int sy = std::min(src.h - 1, static_cast<int>((y + 0.5f) * src.h / out_h));
            for (int x = 0; x < out_w; ++x) {
                int sx = std::min(src.w - 1, static_cast<int>((x + 0.5f) * src.w / out_w));
                std::copy_n(&src.rgba[(static_cast<size_t>(sy) * src.w + sx) * 4], 4, &dst.rgba[(static_cast<size_t>(y) * out_w + x) * 4]);
            }
        }
        return dst;
    }

    // Premultiply into float.
    std::vector<float> pm(static_cast<size_t>(src.w) * src.h * 4);
    for (size_t i = 0; i < pm.size(); i += 4) {
        float a = src.rgba[i + 3] / 255.0f;
        pm[i] = src.rgba[i] / 255.0f * a;
        pm[i + 1] = src.rgba[i + 1] / 255.0f * a;
        pm[i + 2] = src.rgba[i + 2] / 255.0f * a;
        pm[i + 3] = a;
    }
    auto tx = make_taps(src.w, out_w);
    auto ty = make_taps(src.h, out_h);

    std::vector<float> tmp(static_cast<size_t>(out_w) * src.h * 4, 0.0f);
    for (int y = 0; y < src.h; ++y) {
        for (int x = 0; x < out_w; ++x) {
            float acc[4] = {0, 0, 0, 0};
            const Tap& t = tx[x];
            for (size_t k = 0; k < t.w.size(); ++k) {
                int sx = std::clamp(t.first + static_cast<int>(k), 0, src.w - 1);
                const float* p = &pm[(static_cast<size_t>(y) * src.w + sx) * 4];
                for (int c = 0; c < 4; ++c) acc[c] += p[c] * t.w[k];
            }
            std::copy_n(acc, 4, &tmp[(static_cast<size_t>(y) * out_w + x) * 4]);
        }
    }
    for (int y = 0; y < out_h; ++y) {
        const Tap& t = ty[y];
        for (int x = 0; x < out_w; ++x) {
            float acc[4] = {0, 0, 0, 0};
            for (size_t k = 0; k < t.w.size(); ++k) {
                int sy = std::clamp(t.first + static_cast<int>(k), 0, src.h - 1);
                const float* p = &tmp[(static_cast<size_t>(sy) * out_w + x) * 4];
                for (int c = 0; c < 4; ++c) acc[c] += p[c] * t.w[k];
            }
            float a = std::clamp(acc[3], 0.0f, 1.0f);
            std::uint8_t* o = &dst.rgba[(static_cast<size_t>(y) * out_w + x) * 4];
            for (int c = 0; c < 3; ++c) {
                float v = a > 1e-6f ? std::clamp(acc[c] / a, 0.0f, 1.0f) : 0.0f;
                o[c] = static_cast<std::uint8_t>(std::lround(v * 255.0f));
            }
            o[3] = static_cast<std::uint8_t>(std::lround(a * 255.0f));
        }
    }
    return dst;
}

}  // namespace sbso
