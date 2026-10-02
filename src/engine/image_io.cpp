#include "image_io.h"

#include <cstring>

#include "core/vfs.h"
#include "stb_image.h"
#include "webp/decode.h"

namespace sbso::engine {

namespace {
bool is_webp(const std::vector<unsigned char>& d) { return d.size() >= 12 && !std::memcmp(d.data(), "RIFF", 4) && !std::memcmp(d.data() + 8, "WEBP", 4); }
}  // namespace

std::string image_path(const std::string& stem) {
    std::string webp = stem + ".webp";
    return vfs::exists(webp) ? webp : stem + ".png";
}

bool image_dims(const std::string& path, int* w, int* h) {
    std::vector<unsigned char> d;
    if (!vfs::read_prefix(path, 32, &d)) return false;
    if (d.size() >= 24 && d[0] == 0x89 && d[1] == 'P') {  // PNG: IHDR
        auto be = [&](int o) { return static_cast<int>(d[o] << 24 | d[o + 1] << 16 | d[o + 2] << 8 | d[o + 3]); };
        *w = be(16);
        *h = be(20);
        return true;
    }
    if (is_webp(d)) return WebPGetInfo(d.data(), d.size(), w, h) != 0;
    return false;
}

bool decode_image(const std::string& path, int* w, int* h, std::vector<unsigned char>* rgba) {
    std::vector<unsigned char> file;
    if (!vfs::read(path, &file)) return false;
    if (is_webp(file)) {
        uint8_t* px = WebPDecodeRGBA(file.data(), file.size(), w, h);
        if (!px) return false;
        rgba->assign(px, px + static_cast<size_t>(*w) * *h * 4);
        WebPFree(px);
        return true;
    }
    int comp = 0;
    unsigned char* px = stbi_load_from_memory(file.data(), static_cast<int>(file.size()), w, h, &comp, 4);
    if (!px) return false;
    rgba->assign(px, px + static_cast<size_t>(*w) * *h * 4);
    stbi_image_free(px);
    return true;
}

}  // namespace sbso::engine
