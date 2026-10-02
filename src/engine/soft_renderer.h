// CPU reference renderer for DrawCmd lists: used by headless tests/screenshots and as a
// software fallback. Premultiplied-alpha compositing, bilinear sampling, Flash-style masks.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "movieclip.h"
#include "text.h"

namespace sbso::engine {

class SoftRenderer {
public:
    SoftRenderer(int w, int h);

    // images_dir_for(lib) must return the directory with <image id>.png for that library.
    void set_images_root(const std::string& root) { images_root_ = root; }
    void set_text(TextRaster* t) { text_ = t; }
    void clear(float r, float g, float b, float a);
    void draw(const std::vector<DrawCmd>& cmds);
    bool save_png(const std::string& path) const;
    int width() const { return w_; }
    int height() const { return h_; }
    // RGBA8, straight alpha
    std::vector<std::uint8_t> rgba8() const;

private:
    struct Layer { std::vector<float> px; };  // premultiplied RGBA floats
    struct Bitmap { int w = 0, h = 0; std::vector<float> px; };  // premultiplied
    const Bitmap* bitmap(const Library* lib, int id);
    void draw_image(Layer& dst, const DrawCmd& c);
    void blit(Layer& dst, const Bitmap& bm, const Matrix& m, const Rect& src, const ColorTransform& cx, bool smooth);
    void draw_text(Layer& dst, const DrawCmd& c);
    void draw_raw(Layer& dst, const DrawCmd& c);
    void draw_solid(Layer& dst, const DrawCmd& c);
    Layer new_layer() const { return Layer{std::vector<float>(static_cast<size_t>(w_) * h_ * 4, 0.0f)}; }

    int w_, h_;
    std::string images_root_;
    std::vector<Layer> stack_;
    std::map<std::pair<const Library*, int>, Bitmap> cache_;
    TextRaster* text_ = nullptr;
};

}  // namespace sbso::engine
