// Text rasterisation (stb_truetype) for DrawCmd::Kind::Text. Fonts are the TTFs written by
// tools/extract/fonts.py plus fallback faces for glyphs the original fonts lack (e.g. accents).
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "movieclip.h"

namespace sbso::engine {

class FontBank {
public:
    FontBank();
    ~FontBank();
    // `name` is normalised (see normalize_font_name). Returns false if the file cannot be read.
    bool add_ttf(const std::string& name, const std::string& path);
    // Faces tried, in order, for glyphs missing from the requested face.
    void set_fallbacks(std::vector<std::string> names) { fallbacks_ = std::move(names); }
    // Adds every <anything>_<Name>.ttf from a directory written by fonts.py (first file per name wins).
    int add_directory(const std::string& dir);

    struct Face;
    const Face* find(const std::string& name) const;
    const std::vector<std::string>& fallbacks() const { return fallbacks_; }
    // glyph for `cp` and the face providing it (requested face first, then fallbacks); nullptr if none.
    const Face* face_for(const std::string& name, unsigned cp, int* glyph) const;

private:
    std::map<std::string, std::unique_ptr<Face>> faces_;
    std::vector<std::string> fallbacks_;
};

class TextRaster {
public:
    explicit TextRaster(const FontBank* bank) : bank_(bank) {}

    struct Result {
        std::shared_ptr<const RawBitmap> bmp;
        float x = 0, y = 0;  // top-left of the bitmap in the run's local space (logical px)
        float w = 0, h = 0;  // size of the bitmap in the same space
        float scale = 1;     // output pixels per local pixel the bitmap was rasterised at (pixel fonts stay at 1)
    };
    // `scale` = output pixels per local-space pixel at which the text will be shown.
    Result render(const TextRun& run, float scale);
    void clear() { cache_.clear(); }
    // Real word-wrapped layout of `run` (box width and font as drawn): number of lines and the line pitch in local-space pixels.
    struct Metrics { int lines = 0; float line_height = 0; };
    Metrics measure(const TextRun& run) const;

private:
    const FontBank* bank_;
    std::map<std::string, Result> cache_;
};

std::vector<unsigned> decode_utf8(const std::string& s);

}  // namespace sbso::engine
