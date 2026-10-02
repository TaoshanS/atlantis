// CPU image resampling used by the asset tools and as the reference for the GPU shader.
// Pixels are straight-alpha RGBA8 on input/output; filtering is done on premultiplied
// values so transparent edges do not bleed dark or bright halos.
#pragma once
#include <cstdint>
#include <vector>

namespace sbso {

struct Image {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;  // w*h*4, straight alpha
};

enum class Filter { Nearest, Lanczos3 };

Image resample(const Image& src, int out_w, int out_h, Filter filter);

}  // namespace sbso
