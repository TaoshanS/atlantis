#include "soft_renderer.h"
#include "image_io.h"
#include "core/vfs.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "stb_image.h"
#include "stb_image_write.h"

namespace sbso::engine {

SoftRenderer::SoftRenderer(int w, int h) : w_(w), h_(h) { stack_.push_back(new_layer()); }

void SoftRenderer::clear(float r, float g, float b, float a) {
    Layer& l = stack_.front();
    for (size_t i = 0; i < l.px.size(); i += 4) {
        l.px[i] = r * a; l.px[i + 1] = g * a; l.px[i + 2] = b * a; l.px[i + 3] = a;
    }
}

const SoftRenderer::Bitmap* SoftRenderer::bitmap(const Library* lib, int id) {
    auto key = std::make_pair(lib, id);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second.w ? &it->second : nullptr;
    Bitmap b;
    std::string path = lib->image_file(images_root_, id);
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
    if (decode_image(path, &w, &h, &rgba)) {
        const unsigned char* data = rgba.data();
        b.w = w; b.h = h;
        b.px.resize(static_cast<size_t>(w) * h * 4);
        for (size_t i = 0; i < b.px.size(); i += 4) {
            float a = data[i + 3] / 255.0f;
            b.px[i] = data[i] / 255.0f * a; b.px[i + 1] = data[i + 1] / 255.0f * a; b.px[i + 2] = data[i + 2] / 255.0f * a; b.px[i + 3] = a;
        }
    }
    auto& slot = cache_[key] = std::move(b);
    return slot.w ? &slot : nullptr;
}

static void blend(float* dst, float r, float g, float b, float a) {
    float inv = 1.0f - a;
    dst[0] = r + dst[0] * inv; dst[1] = g + dst[1] * inv; dst[2] = b + dst[2] * inv; dst[3] = a + dst[3] * inv;
}

void SoftRenderer::draw_image(Layer& dst, const DrawCmd& c) {
    const Bitmap* bm = bitmap(c.lib, c.image);
    if (!bm) return;
    blit(dst, *bm, c.m, c.src, c.cx, c.smooth);
}

void SoftRenderer::draw_raw(Layer& dst, const DrawCmd& c) {
    if (!c.raw) return;
    Bitmap bm;
    bm.w = c.raw->w; bm.h = c.raw->h;
    bm.px.resize(c.raw->rgba.size());
    for (size_t i = 0; i < bm.px.size(); i += 4) {
        float a = c.raw->rgba[i + 3] / 255.0f;
        bm.px[i] = c.raw->rgba[i] / 255.0f * a; bm.px[i + 1] = c.raw->rgba[i + 1] / 255.0f * a; bm.px[i + 2] = c.raw->rgba[i + 2] / 255.0f * a; bm.px[i + 3] = a;
    }
    blit(dst, bm, c.m, Rect{0, 0, static_cast<float>(bm.w), static_cast<float>(bm.h)}, c.cx, c.smooth);
}

void SoftRenderer::draw_text(Layer& dst, const DrawCmd& c) {
    if (!text_) return;
    float s = std::sqrt(std::fabs(c.m.a * c.m.d - c.m.b * c.m.c));
    if (s <= 0) return;
    TextRaster::Result r = text_->render(c.run, s);
    if (!r.bmp) return;
    Bitmap bm;
    bm.w = r.bmp->w; bm.h = r.bmp->h;
    bm.px.resize(r.bmp->rgba.size());
    for (size_t i = 0; i < bm.px.size(); i += 4) {
        float a = r.bmp->rgba[i + 3] / 255.0f;
        bm.px[i] = r.bmp->rgba[i] / 255.0f * a; bm.px[i + 1] = r.bmp->rgba[i + 1] / 255.0f * a; bm.px[i + 2] = r.bmp->rgba[i + 2] / 255.0f * a; bm.px[i + 3] = a;
    }
    // bitmap pixels -> local space (1/s each, offset by the bitmap origin) -> output
    Matrix local{1.0f / r.scale, 0, 0, 1.0f / r.scale, r.x, r.y};
    blit(dst, bm, c.m * local, Rect{0, 0, static_cast<float>(bm.w), static_cast<float>(bm.h)}, c.cx, false);
}

void SoftRenderer::blit(Layer& dst, const Bitmap& bmref, const Matrix& cm, const Rect& csrc, const ColorTransform& ccx, bool csmooth) {
    const Bitmap* bm = &bmref;
    if (!cm.invertible()) return;
    struct { Matrix m; Rect src; ColorTransform cx; bool smooth; } c{cm, csrc, ccx, csmooth};
    Matrix inv = c.m.inverse();
    // bounding box of the transformed source rect
    Point p[4] = {c.m.apply({c.src.x0, c.src.y0}), c.m.apply({c.src.x1, c.src.y0}), c.m.apply({c.src.x1, c.src.y1}), c.m.apply({c.src.x0, c.src.y1})};
    float minx = p[0].x, maxx = p[0].x, miny = p[0].y, maxy = p[0].y;
    for (auto& q : p) { minx = std::min(minx, q.x); maxx = std::max(maxx, q.x); miny = std::min(miny, q.y); maxy = std::max(maxy, q.y); }
    int x0 = std::max(0, static_cast<int>(std::floor(minx))), x1 = std::min(w_, static_cast<int>(std::ceil(maxx)));
    int y0 = std::max(0, static_cast<int>(std::floor(miny))), y1 = std::min(h_, static_cast<int>(std::ceil(maxy)));
    const bool smooth = c.smooth;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            Point s = inv.apply({x + 0.5f, y + 0.5f});
            if (s.x < c.src.x0 || s.x >= c.src.x1 || s.y < c.src.y0 || s.y >= c.src.y1) continue;
            float r, g, b, a;
            auto fetch = [&](int ix, int iy, float* o) {
                ix = std::max(static_cast<int>(std::floor(c.src.x0)), std::min(ix, static_cast<int>(std::ceil(c.src.x1)) - 1));
                iy = std::max(static_cast<int>(std::floor(c.src.y0)), std::min(iy, static_cast<int>(std::ceil(c.src.y1)) - 1));
                ix = std::max(0, std::min(ix, bm->w - 1)); iy = std::max(0, std::min(iy, bm->h - 1));
                const float* q = &bm->px[(static_cast<size_t>(iy) * bm->w + ix) * 4];
                o[0] = q[0]; o[1] = q[1]; o[2] = q[2]; o[3] = q[3];
            };
            float t[4];
            if (smooth && !(std::fabs(c.m.a) == 1.0f && std::fabs(c.m.d) == 1.0f && c.m.axis_aligned() && std::fabs(s.x - std::floor(s.x) - 0.5f) < 1e-3f && std::fabs(s.y - std::floor(s.y) - 0.5f) < 1e-3f)) {
                float fx = s.x - 0.5f, fy = s.y - 0.5f;
                int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
                float ax = fx - ix, ay = fy - iy;
                float a00[4], a10[4], a01[4], a11[4];
                fetch(ix, iy, a00); fetch(ix + 1, iy, a10); fetch(ix, iy + 1, a01); fetch(ix + 1, iy + 1, a11);
                for (int k = 0; k < 4; ++k) t[k] = (a00[k] * (1 - ax) + a10[k] * ax) * (1 - ay) + (a01[k] * (1 - ax) + a11[k] * ax) * ay;
            } else {
                fetch(static_cast<int>(std::floor(s.x)), static_cast<int>(std::floor(s.y)), t);
            }
            r = t[0]; g = t[1]; b = t[2]; a = t[3];
            if (!c.cx.identity()) {
                // colour transform operates on straight alpha
                float sr = a > 0 ? r / a : 0, sg = a > 0 ? g / a : 0, sb = a > 0 ? b / a : 0;
                sr = std::clamp(sr * c.cx.mult[0] + c.cx.add[0] / 255.0f, 0.0f, 1.0f);
                sg = std::clamp(sg * c.cx.mult[1] + c.cx.add[1] / 255.0f, 0.0f, 1.0f);
                sb = std::clamp(sb * c.cx.mult[2] + c.cx.add[2] / 255.0f, 0.0f, 1.0f);
                a = std::clamp(a * c.cx.mult[3] + c.cx.add[3] / 255.0f, 0.0f, 1.0f);
                r = sr * a; g = sg * a; b = sb * a;
            }
            if (a <= 0) continue;
            blend(&dst.px[(static_cast<size_t>(y) * w_ + x) * 4], r, g, b, a);
        }
    }
}

void SoftRenderer::draw_solid(Layer& dst, const DrawCmd& c) {
    if (!c.solid) return;
    for (const auto& contour : c.solid->contours) {
        if (contour.size() < 3) continue;
        std::vector<Point> pts;
        for (const Point& p : contour) pts.push_back(c.m.apply(p));
        float miny = pts[0].y, maxy = pts[0].y;
        for (auto& p : pts) { miny = std::min(miny, p.y); maxy = std::max(maxy, p.y); }
        int y0 = std::max(0, static_cast<int>(std::floor(miny))), y1 = std::min(h_ - 1, static_cast<int>(std::ceil(maxy)));
        float a = c.solid->rgba[3] * c.cx.mult[3] + c.cx.add[3] / 255.0f;
        float r = std::clamp(c.solid->rgba[0] * c.cx.mult[0] + c.cx.add[0] / 255.0f, 0.0f, 1.0f);
        float g = std::clamp(c.solid->rgba[1] * c.cx.mult[1] + c.cx.add[1] / 255.0f, 0.0f, 1.0f);
        float b = std::clamp(c.solid->rgba[2] * c.cx.mult[2] + c.cx.add[2] / 255.0f, 0.0f, 1.0f);
        a = std::clamp(a, 0.0f, 1.0f);
        for (int y = y0; y <= y1; ++y) {
            float sy = y + 0.5f;
            std::vector<float> xs;
            for (size_t i = 0; i < pts.size(); ++i) {
                const Point& p = pts[i];
                const Point& q = pts[(i + 1) % pts.size()];
                if ((p.y <= sy && q.y > sy) || (q.y <= sy && p.y > sy)) xs.push_back(p.x + (sy - p.y) / (q.y - p.y) * (q.x - p.x));
            }
            std::sort(xs.begin(), xs.end());
            for (size_t k = 0; k + 1 < xs.size(); k += 2) {
                int xa = std::max(0, static_cast<int>(std::floor(xs[k] + 0.5f))), xb = std::min(w_, static_cast<int>(std::floor(xs[k + 1] + 0.5f)));
                for (int x = xa; x < xb; ++x) blend(&dst.px[(static_cast<size_t>(y) * w_ + x) * 4], r * a, g * a, b * a, a);
            }
        }
    }
}

void SoftRenderer::draw(const std::vector<DrawCmd>& cmds) {
    struct MaskState { Layer mask; bool applied = false; };
    std::vector<MaskState> masks;
    auto top = [&]() -> Layer& { return stack_.back(); };
    for (const DrawCmd& c : cmds) {
        switch (c.kind) {
            case DrawCmd::Kind::Image: draw_image(top(), c); break;
            case DrawCmd::Kind::Solid: draw_solid(top(), c); break;
            case DrawCmd::Kind::Text: draw_text(top(), c); break;
            case DrawCmd::Kind::Raw: draw_raw(top(), c); break;
            case DrawCmd::Kind::MaskBegin:
                masks.push_back(MaskState{new_layer(), false});
                stack_.push_back(new_layer());  // mask shape layer
                break;
            case DrawCmd::Kind::MaskApply:
                masks.back().mask = std::move(stack_.back());
                stack_.pop_back();
                masks.back().applied = true;
                stack_.push_back(new_layer());  // masked content layer
                break;
            case DrawCmd::Kind::MaskEnd: {
                if (masks.empty()) break;
                Layer content = std::move(stack_.back());
                stack_.pop_back();
                const Layer& m = masks.back().mask;
                for (size_t i = 0; i < content.px.size(); i += 4) {
                    float k = m.px[i + 3];
                    content.px[i] *= k; content.px[i + 1] *= k; content.px[i + 2] *= k; content.px[i + 3] *= k;
                }
                Layer& dst = top();
                for (size_t i = 0; i < dst.px.size(); i += 4) blend(&dst.px[i], content.px[i], content.px[i + 1], content.px[i + 2], content.px[i + 3]);
                masks.pop_back();
                break;
            }
        }
    }
}

std::vector<std::uint8_t> SoftRenderer::rgba8() const {
    const Layer& l = stack_.front();
    std::vector<std::uint8_t> out(l.px.size());
    for (size_t i = 0; i < l.px.size(); i += 4) {
        float a = l.px[i + 3];
        for (int k = 0; k < 3; ++k) out[i + k] = static_cast<std::uint8_t>(std::lround(std::clamp(a > 0 ? l.px[i + k] / a : 0.0f, 0.0f, 1.0f) * 255.0f));
        out[i + 3] = static_cast<std::uint8_t>(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f));
    }
    return out;
}

bool SoftRenderer::save_png(const std::string& path) const {
    auto px = rgba8();
    return stbi_write_png(path.c_str(), w_, h_, 4, px.data(), w_ * 4) != 0;
}

}  // namespace sbso::engine
