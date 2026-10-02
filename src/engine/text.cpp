#include "text.h"
#include "core/vfs.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "stb_truetype.h"

namespace sbso::engine {

struct FontBank::Face {
    std::vector<unsigned char> data;
    stbtt_fontinfo info;
    int ascent = 0, descent = 0, line_gap = 0;  // font units
};

FontBank::FontBank() = default;
FontBank::~FontBank() = default;

bool FontBank::add_ttf(const std::string& name, const std::string& path) {
    auto face = std::make_unique<Face>();
    if (!vfs::read(path, &face->data)) return false;
    if (!stbtt_InitFont(&face->info, face->data.data(), stbtt_GetFontOffsetForIndex(face->data.data(), 0))) return false;
    stbtt_GetFontVMetrics(&face->info, &face->ascent, &face->descent, &face->line_gap);
    faces_[name] = std::move(face);
    return true;
}

int FontBank::add_directory(const std::string& dir) {
    int n = 0;
    for (const std::string& file : vfs::list(dir)) {
        std::filesystem::path fp(file);
        if (fp.extension() != ".ttf") continue;
        std::string stem = fp.stem().string();
        size_t us = stem.rfind('_');
        std::string name = normalize_font_name(us == std::string::npos ? stem : stem.substr(us + 1));
        // the same family is embedded by several SWFs, some with only a handful of glyphs: keep the face with the most glyphs
        auto old = faces_.find(name);
        std::string path = dir + "/" + file;
        if (old != faces_.end()) {
            std::vector<unsigned char> data;
            stbtt_fontinfo info;
            if (!vfs::read(path, &data) || !stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0))) continue;
            if (info.numGlyphs <= old->second->info.numGlyphs) continue;
        }
        if (add_ttf(name, path)) ++n;
    }
    return n;
}

const FontBank::Face* FontBank::find(const std::string& name) const {
    auto it = faces_.find(name);
    return it == faces_.end() ? nullptr : it->second.get();
}

const FontBank::Face* FontBank::face_for(const std::string& name, unsigned cp, int* glyph) const {
    auto try_face = [&](const Face* f) -> const Face* {
        if (!f) return nullptr;
        int g = stbtt_FindGlyphIndex(&f->info, static_cast<int>(cp));
        if (g) { *glyph = g; return f; }
        return nullptr;
    };
    if (const Face* f = try_face(find(name))) return f;
    for (const auto& fb : fallbacks_)
        if (const Face* f = try_face(find(fb))) return f;
    return nullptr;
}

std::vector<unsigned> decode_utf8(const std::string& s) {
    std::vector<unsigned> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned cp = c;
        int n = 0;
        if (c >= 0xF0) { cp = c & 0x07; n = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; n = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; n = 1; }
        ++i;
        for (int k = 0; k < n && i < s.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        out.push_back(cp);
    }
    return out;
}

namespace {

struct Glyph {
    unsigned cp;
    const FontBank::Face* face = nullptr;
    int index = 0;
    float advance = 0;  // output pixels
};

struct Line {
    std::vector<Glyph> glyphs;
    float width = 0;
};

// Word-wraps `run` and returns its lines plus the vertical metrics (output pixels at `scale`).
void layout_run(const FontBank* bank_, const TextRun& run, float scale, std::vector<Line>* out_lines, float* out_asc, float* out_desc, float* out_gap) {
    auto cp = decode_utf8(run.text);
    float px = run.size * scale;  // font size in output pixels

    auto layout_glyph = [&](unsigned c, size_t idx) {
        Glyph g;
        g.cp = c;
        g.face = bank_ ? bank_->face_for(run.font, c, &g.index) : nullptr;
        if (g.face) {
            float s = stbtt_ScaleForMappingEmToPixels(&g.face->info, px);
            int adv = 0, lsb = 0;
            stbtt_GetGlyphHMetrics(&g.face->info, g.index, &adv, &lsb);
            g.advance = adv * s;
        } else {
            g.advance = px * 0.5f;
        }
        if (idx < run.advances.size()) g.advance = run.advances[idx] * scale;
        return g;
    };

    std::vector<Line>& lines = *out_lines;
    float box_w_px = run.has_origin ? 1e9f : (run.box.w() - 4.0f) * scale;  // 2 px gutter each side
    {
        Line cur;
        size_t last_space = std::string::npos;  // index into cur.glyphs
        for (size_t i = 0; i < cp.size(); ++i) {
            unsigned c = cp[i];
            if (c == '\r') continue;
            if (c == '\n') { lines.push_back(std::move(cur)); cur = Line{}; last_space = std::string::npos; continue; }
            Glyph g = layout_glyph(c, i);
            if (run.wrap && !run.has_origin && cur.width + g.advance > box_w_px && !cur.glyphs.empty()) {
                if (last_space != std::string::npos && c != ' ') {
                    Line next;
                    next.glyphs.assign(cur.glyphs.begin() + static_cast<long>(last_space) + 1, cur.glyphs.end());
                    cur.glyphs.resize(last_space);
                    cur.width = 0;
                    for (auto& x : cur.glyphs) cur.width += x.advance;
                    for (auto& x : next.glyphs) next.width += x.advance;
                    lines.push_back(std::move(cur));
                    cur = std::move(next);
                } else {
                    lines.push_back(std::move(cur));
                    cur = Line{};
                }
                last_space = std::string::npos;
                if (c == ' ') continue;
            }
            if (c == ' ') last_space = cur.glyphs.size();
            cur.glyphs.push_back(g);
            cur.width += g.advance;
        }
        lines.push_back(std::move(cur));
    }

    // Vertical metrics come from the primary (or first fallback) face.
    const FontBank::Face* metric_face = bank_ ? bank_->find(run.font) : nullptr;
    if (!metric_face && bank_ && !bank_->fallbacks().empty()) metric_face = bank_->find(bank_->fallbacks().front());
    float asc = px * 0.8f, desc = px * 0.2f, gap = 0;
    if (metric_face) {
        float s = stbtt_ScaleForMappingEmToPixels(&metric_face->info, px);
        asc = metric_face->ascent * s; desc = -metric_face->descent * s; gap = metric_face->line_gap * s;
    }
    if (run.line_height > 0) gap = run.line_height * scale - asc - desc;  // fixed pitch: the difference is leading
    *out_asc = asc; *out_desc = desc; *out_gap = gap;

}

}  // namespace

TextRaster::Metrics TextRaster::measure(const TextRun& run) const {
    std::vector<Line> lines;
    float asc = 0, desc = 0, gap = 0;
    layout_run(bank_, run, 1.0f, &lines, &asc, &desc, &gap);
    Metrics m;
    m.lines = static_cast<int>(lines.size());
    m.line_height = asc + desc + gap;
    return m;
}

TextRaster::Result TextRaster::render(const TextRun& run, float scale) {
    // Unibody 8 is a pixel font drawn on an 8 px grid: rasterise it at 1x and let the renderer scale the bitmap like any other art
    if (run.font == "unibody8black" && run.size <= 10) scale = 1.0f;
    std::ostringstream key;
    key << run.font << '|' << run.size << '|' << run.color[0] << ',' << run.color[1] << ',' << run.color[2] << ',' << run.color[3] << '|'
        << run.box.x0 << ',' << run.box.y0 << ',' << run.box.x1 << ',' << run.box.y1 << '|' << run.align << run.wrap << run.multiline << '|' << run.has_origin << run.ox << ',' << run.oy
        << '|' << std::lround(scale * 100) << '|' << run.line_height << '|' << run.text;
    for (float a : run.advances) key << ',' << a;
    auto it = cache_.find(key.str());
    if (it != cache_.end()) return it->second;

    Result res;
    float px = run.size * scale;  // font size in output pixels
    std::vector<Line> lines;
    float asc = 0, desc = 0, gap = 0;
    layout_run(bank_, run, scale, &lines, &asc, &desc, &gap);
    float line_h = asc + desc + gap;

    // Output bitmap geometry (in output pixels, relative to the run's origin in local space).
    float left, top, width, height;
    if (run.has_origin) {
        float w = lines.empty() ? 0 : lines[0].width;
        left = run.ox * scale - 2; top = run.oy * scale - asc - 2; width = w + 4 + px * 0.2f; height = asc + desc + 4;
    } else {
        left = run.box.x0 * scale; top = run.box.y0 * scale; width = run.box.w() * scale; height = run.box.h() * scale;
        height = std::max(height, lines.size() * line_h + 4 * scale);
    }
    int bw = std::max(1, static_cast<int>(std::ceil(width))), bh = std::max(1, static_cast<int>(std::ceil(height)));
    bw = std::min(bw, 4096); bh = std::min(bh, 4096);
    auto bmp = std::make_shared<RawBitmap>();
    bmp->w = bw; bmp->h = bh;
    bmp->rgba.assign(static_cast<size_t>(bw) * bh * 4, 0);
    auto cov = std::vector<float>(static_cast<size_t>(bw) * bh, 0.0f);

    float baseline = run.has_origin ? run.oy * scale - top : 2 * scale + asc;
    for (const Line& ln : lines) {
        float x;
        if (run.has_origin) x = run.ox * scale - left;
        else if (run.align == 1) x = bw - 2 * scale - ln.width;
        else if (run.align == 2) x = (bw - ln.width) * 0.5f;
        else x = 2 * scale;
        for (const Glyph& g : ln.glyphs) {
            if (g.face && g.cp != ' ') {
                float s = stbtt_ScaleForMappingEmToPixels(&g.face->info, px);
                int gx0, gy0, gx1, gy1;
                float fx = x - std::floor(x);
                stbtt_GetGlyphBitmapBoxSubpixel(&g.face->info, g.index, s, s, fx, 0, &gx0, &gy0, &gx1, &gy1);
                int gw = gx1 - gx0, gh = gy1 - gy0;
                if (gw > 0 && gh > 0) {
                    std::vector<unsigned char> tmp(static_cast<size_t>(gw) * gh);
                    stbtt_MakeGlyphBitmapSubpixel(&g.face->info, tmp.data(), gw, gh, gw, s, s, fx, 0, g.index);
                    int ox = static_cast<int>(std::floor(x)) + gx0, oy = static_cast<int>(std::lround(baseline)) + gy0;
                    for (int yy = 0; yy < gh; ++yy)
                        for (int xx = 0; xx < gw; ++xx) {
                            int dx = ox + xx, dy = oy + yy;
                            if (dx < 0 || dy < 0 || dx >= bw || dy >= bh) continue;
                            float a = tmp[static_cast<size_t>(yy) * gw + xx] / 255.0f;
                            float& c = cov[static_cast<size_t>(dy) * bw + dx];
                            c = c + a - c * a;
                        }
                }
            }
            x += g.advance;
        }
        baseline += line_h;
        if (!run.multiline && !run.wrap && !run.has_origin) break;
    }
    for (size_t i = 0; i < cov.size(); ++i) {
        float a = std::clamp(cov[i] * run.color[3], 0.0f, 1.0f);
        bmp->rgba[i * 4] = static_cast<unsigned char>(std::lround(run.color[0] * 255.0f));
        bmp->rgba[i * 4 + 1] = static_cast<unsigned char>(std::lround(run.color[1] * 255.0f));
        bmp->rgba[i * 4 + 2] = static_cast<unsigned char>(std::lround(run.color[2] * 255.0f));
        bmp->rgba[i * 4 + 3] = static_cast<unsigned char>(std::lround(a * 255.0f));
    }
    res.bmp = bmp;
    res.x = left / scale; res.y = top / scale; res.w = bw / scale; res.h = bh / scale; res.scale = scale;
    cache_[key.str()] = res;
    return res;
}

}  // namespace sbso::engine
