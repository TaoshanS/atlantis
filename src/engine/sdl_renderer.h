// SDL3 backend for DrawCmd lists. Works on every SDL_Renderer driver (Metal, Vulkan, D3D, GL,
// software). Bitmaps are uploaded premultiplied; the "Lanczos" asset filter pre-scales each
// bitmap on the CPU (see core/resample.h) so it needs no shaders.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "core/resample.h"
#include "movieclip.h"
#include "text.h"

struct SDL_Renderer;
struct SDL_Texture;

namespace sbso::engine {

class SdlRenderer {
public:
    SdlRenderer(SDL_Renderer* r, std::string images_root) : r_(r), images_root_(std::move(images_root)) {}
    ~SdlRenderer();

    // Asset filter: scale > 1 with Lanczos pre-scales bitmaps; Nearest keeps originals (integer scale).
    void set_asset_filter(float scale, sbso::Filter filter);
    // `view` maps root (logical) coordinates to output pixels.
    void draw(const std::vector<DrawCmd>& cmds, const Matrix& view, int out_w, int out_h);
    void set_text(TextRaster* t) { text_ = t; }
    void clear_cache();
    // Reads back the current render target as RGBA8 (straight alpha). Returns false on failure.
    bool read_pixels(int* w, int* h, std::vector<unsigned char>* rgba);

private:
    struct Tex { SDL_Texture* tex = nullptr; int w = 0, h = 0; float scale = 1; };
    const Tex* texture(const Library* lib, int id);
    SDL_Texture* make_target(int w, int h);
    bool custom_blend_ok(int w, int h);
    void draw_image(const DrawCmd& c, const Matrix& view);
    void draw_solid(const DrawCmd& c, const Matrix& view);
    void draw_text(const DrawCmd& c, const Matrix& view);
    void draw_raw(const DrawCmd& c, const Matrix& view);

    SDL_Renderer* r_;
    std::string images_root_;
    float asset_scale_ = 1.0f;
    sbso::Filter filter_ = sbso::Filter::Nearest;
    std::map<std::pair<const Library*, int>, Tex> cache_;
    struct TextTex { std::shared_ptr<const RawBitmap> keep; SDL_Texture* tex = nullptr; };
    std::map<const RawBitmap*, TextTex> text_cache_;
    std::map<const RawBitmap*, TextTex> raw_cache_;  // baked layers, prescaled like images
    TextRaster* text_ = nullptr;
    std::vector<SDL_Texture*> target_pool_;
    int pool_w_ = 0, pool_h_ = 0;
    int custom_ok_ = -1;  // -1 unknown, 0 unsupported (e.g. software renderer), 1 supported
    std::vector<SDL_Texture*> target_stack_;  // currently bound targets (mask nesting); nullptr = backbuffer
};

}  // namespace sbso::engine
