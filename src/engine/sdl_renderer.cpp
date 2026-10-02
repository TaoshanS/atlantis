#include <cstdio>
#include <cstdlib>
#include "sdl_renderer.h"
#include "core/vfs.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "stb_image.h"

namespace sbso::engine {
namespace {

SDL_BlendMode multiply_by_src_alpha() {
    static SDL_BlendMode mode = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                                          SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    return mode;
}

float cross(Point a, Point b, Point c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }

bool in_triangle(Point p, Point a, Point b, Point c) {
    float d1 = cross(p, a, b), d2 = cross(p, b, c), d3 = cross(p, c, a);
    bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

// Ear clipping for a simple polygon; returns triangle index triples into `pts`.
std::vector<int> triangulate(const std::vector<Point>& pts) {
    std::vector<int> idx(pts.size()), tris;
    for (size_t i = 0; i < pts.size(); ++i) idx[i] = static_cast<int>(i);
    float area = 0;
    for (size_t i = 0; i < pts.size(); ++i) { const Point& p = pts[i]; const Point& q = pts[(i + 1) % pts.size()]; area += p.x * q.y - q.x * p.y; }
    if (area < 0) std::reverse(idx.begin(), idx.end());
    int guard = 0;
    while (idx.size() > 3 && guard++ < 10000) {
        bool clipped = false;
        for (size_t i = 0; i < idx.size(); ++i) {
            int a = idx[(i + idx.size() - 1) % idx.size()], b = idx[i], c = idx[(i + 1) % idx.size()];
            if (cross(pts[a], pts[b], pts[c]) <= 0) continue;
            bool ear = true;
            for (int k : idx) {
                if (k == a || k == b || k == c) continue;
                if (in_triangle(pts[k], pts[a], pts[b], pts[c])) { ear = false; break; }
            }
            if (!ear) continue;
            tris.insert(tris.end(), {a, b, c});
            idx.erase(idx.begin() + static_cast<long>(i));
            clipped = true;
            break;
        }
        if (!clipped) break;  // degenerate polygon: fall back to a fan below
    }
    if (idx.size() >= 3) {
        for (size_t i = 1; i + 1 < idx.size(); ++i) tris.insert(tris.end(), {idx[0], idx[i], idx[i + 1]});
    }
    return tris;
}

}  // namespace

SdlRenderer::~SdlRenderer() {
    clear_cache();
    for (SDL_Texture* t : target_pool_) SDL_DestroyTexture(t);
}

void SdlRenderer::clear_cache() {
    for (auto& [k, t] : cache_)
        if (t.tex) SDL_DestroyTexture(t.tex);
    cache_.clear();
    for (auto& [k, t] : text_cache_)
        if (t.tex) SDL_DestroyTexture(t.tex);
    text_cache_.clear();
    for (auto& [k, t] : raw_cache_)
        if (t.tex) SDL_DestroyTexture(t.tex);
    raw_cache_.clear();
    if (text_) text_->clear();
}

void SdlRenderer::draw_raw(const DrawCmd& c, const Matrix& view) {
    if (!c.raw || c.raw->w <= 0) return;
    auto it = raw_cache_.find(c.raw.get());
    if (it == raw_cache_.end()) {
        sbso::Image img;
        img.w = c.raw->w;
        img.h = c.raw->h;
        img.rgba = c.raw->rgba;
        if (filter_ == sbso::Filter::Lanczos3 && asset_scale_ > 1.01f) {
            int ow = std::max(1, static_cast<int>(std::lround(img.w * asset_scale_))), oh = std::max(1, static_cast<int>(std::lround(img.h * asset_scale_)));
            img = sbso::resample(img, ow, oh, sbso::Filter::Lanczos3);
        }
        for (size_t i = 0; i < img.rgba.size(); i += 4) {
            unsigned a = img.rgba[i + 3];
            img.rgba[i] = static_cast<unsigned char>((img.rgba[i] * a + 127) / 255);
            img.rgba[i + 1] = static_cast<unsigned char>((img.rgba[i + 1] * a + 127) / 255);
            img.rgba[i + 2] = static_cast<unsigned char>((img.rgba[i + 2] * a + 127) / 255);
        }
        SDL_Texture* tex = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.w, img.h);
        if (!tex) return;
        SDL_UpdateTexture(tex, nullptr, img.rgba.data(), img.w * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        it = raw_cache_.emplace(c.raw.get(), TextTex{c.raw, tex}).first;
    }
    Matrix m = view * c.m;
    float w = static_cast<float>(c.raw->w), h = static_cast<float>(c.raw->h);
    float ma = c.cx.mult[3];
    SDL_FColor col{c.cx.mult[0] * ma, c.cx.mult[1] * ma, c.cx.mult[2] * ma, ma};
    Point p[4] = {m.apply({0, 0}), m.apply({w, 0}), m.apply({w, h}), m.apply({0, h})};
    SDL_Vertex v[4] = {{{p[0].x, p[0].y}, col, {0, 0}}, {{p[1].x, p[1].y}, col, {1, 0}}, {{p[2].x, p[2].y}, col, {1, 1}}, {{p[3].x, p[3].y}, col, {0, 1}}};
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r_, it->second.tex, v, 4, idx, 6);
}

void SdlRenderer::draw_text(const DrawCmd& c, const Matrix& view) {
    if (!text_) return;
    Matrix m = view * c.m;
    float s = std::sqrt(std::fabs(m.a * m.d - m.b * m.c));
    if (s <= 0) return;
    TextRaster::Result r = text_->render(c.run, s);
    if (!r.bmp) return;
    auto it = text_cache_.find(r.bmp.get());
    if (it == text_cache_.end()) {
        std::vector<unsigned char> px(r.bmp->rgba);
        for (size_t i = 0; i < px.size(); i += 4) {
            unsigned a = px[i + 3];
            px[i] = static_cast<unsigned char>((px[i] * a + 127) / 255);
            px[i + 1] = static_cast<unsigned char>((px[i + 1] * a + 127) / 255);
            px[i + 2] = static_cast<unsigned char>((px[i + 2] * a + 127) / 255);
        }
        SDL_Texture* tex = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, r.bmp->w, r.bmp->h);
        if (!tex) return;
        SDL_UpdateTexture(tex, nullptr, px.data(), r.bmp->w * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        it = text_cache_.emplace(r.bmp.get(), TextTex{r.bmp, tex}).first;
    }
    float ma = c.cx.mult[3];
    SDL_FColor col{c.cx.mult[0] * ma, c.cx.mult[1] * ma, c.cx.mult[2] * ma, ma};
    Matrix local{1.0f / r.scale, 0, 0, 1.0f / r.scale, r.x, r.y};
    Matrix full = c.m * local;
    Matrix mm = view * full;
    float w = static_cast<float>(r.bmp->w), h = static_cast<float>(r.bmp->h);
    Point p[4] = {mm.apply({0, 0}), mm.apply({w, 0}), mm.apply({w, h}), mm.apply({0, h})};
    SDL_Vertex v[4] = {{{p[0].x, p[0].y}, col, {0, 0}}, {{p[1].x, p[1].y}, col, {1, 0}}, {{p[2].x, p[2].y}, col, {1, 1}}, {{p[3].x, p[3].y}, col, {0, 1}}};
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r_, it->second.tex, v, 4, idx, 6);
}

void SdlRenderer::set_asset_filter(float scale, sbso::Filter filter) {
    if (scale == asset_scale_ && filter == filter_) return;
    asset_scale_ = scale;
    filter_ = filter;
    clear_cache();
}

const SdlRenderer::Tex* SdlRenderer::texture(const Library* lib, int id) {
    auto key = std::make_pair(lib, id);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second.tex ? &it->second : nullptr;
    Tex t;
    int w = 0, h = 0, comp = 0;
    std::vector<unsigned char> file;
    unsigned char* data = vfs::read(lib->image_file(images_root_, id), &file)
                              ? stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &w, &h, &comp, 4) : nullptr;
    if (data) {
        sbso::Image img;
        img.w = w;
        img.h = h;
        img.rgba.assign(data, data + static_cast<size_t>(w) * h * 4);
        stbi_image_free(data);
        if (filter_ == sbso::Filter::Lanczos3 && asset_scale_ > 1.01f) {
            float k = std::min(1.0f, 8192.0f / (std::max(w, h) * asset_scale_));  // stay within the GPU texture size limit
            int ow = std::max(1, static_cast<int>(std::lround(w * asset_scale_ * k))), oh = std::max(1, static_cast<int>(std::lround(h * asset_scale_ * k)));
            img = sbso::resample(img, ow, oh, sbso::Filter::Lanczos3);
        }
        // upload premultiplied
        for (size_t i = 0; i < img.rgba.size(); i += 4) {
            unsigned a = img.rgba[i + 3];
            img.rgba[i] = static_cast<unsigned char>((img.rgba[i] * a + 127) / 255);
            img.rgba[i + 1] = static_cast<unsigned char>((img.rgba[i + 1] * a + 127) / 255);
            img.rgba[i + 2] = static_cast<unsigned char>((img.rgba[i + 2] * a + 127) / 255);
        }
        SDL_Texture* tex = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.w, img.h);
        if (tex) {
            SDL_UpdateTexture(tex, nullptr, img.rgba.data(), img.w * 4);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
            t.tex = tex;
            t.w = img.w;
            t.h = img.h;
            t.scale = static_cast<float>(img.w) / w;
        }
    }
    auto& slot = cache_[key] = t;
    return slot.tex ? &slot : nullptr;
}

SDL_Texture* SdlRenderer::make_target(int w, int h) {
    if (w != pool_w_ || h != pool_h_) {
        for (SDL_Texture* t : target_pool_) SDL_DestroyTexture(t);
        target_pool_.clear();
        pool_w_ = w;
        pool_h_ = h;
    }
    if (!target_pool_.empty()) {
        SDL_Texture* t = target_pool_.back();
        target_pool_.pop_back();
        return t;
    }
    SDL_Texture* t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    return t;
}

void SdlRenderer::draw_image(const DrawCmd& c, const Matrix& view) {
    const Tex* t = texture(c.lib, c.image);
    if (!t) return;
    int iw = 0, ih = 0;
    if (!c.lib->image_size(c.image, &iw, &ih) || iw <= 0) return;
    Matrix m = view * c.m;
    SDL_SetTextureScaleMode(t->tex, c.smooth && filter_ != sbso::Filter::Nearest ? SDL_SCALEMODE_LINEAR : (c.smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST));
    float ma = c.cx.mult[3];
    SDL_FColor col{c.cx.mult[0] * ma, c.cx.mult[1] * ma, c.cx.mult[2] * ma, ma};
    Point p[4] = {m.apply({c.src.x0, c.src.y0}), m.apply({c.src.x1, c.src.y0}), m.apply({c.src.x1, c.src.y1}), m.apply({c.src.x0, c.src.y1})};
    float u0 = c.src.x0 / iw, u1 = c.src.x1 / iw, v0 = c.src.y0 / ih, v1 = c.src.y1 / ih;
    SDL_Vertex v[4] = {{{p[0].x, p[0].y}, col, {u0, v0}}, {{p[1].x, p[1].y}, col, {u1, v0}}, {{p[2].x, p[2].y}, col, {u1, v1}}, {{p[3].x, p[3].y}, col, {u0, v1}}};
    // Mirrored placements reverse the winding, which the software rasterizer draws wrongly: relabel the corners so the
    // vertex order is always the same way round (positions and UVs stay paired).
    if (m.a * m.d - m.b * m.c < 0) {
        std::swap(v[0], v[1]);
        std::swap(v[2], v[3]);
    }
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r_, t->tex, v, 4, idx, 6);
}

void SdlRenderer::draw_solid(const DrawCmd& c, const Matrix& view) {
    if (!c.solid) return;
    Matrix m = view * c.m;
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    float a = std::clamp(c.solid->rgba[3] * c.cx.mult[3] + c.cx.add[3] / 255.0f, 0.0f, 1.0f);
    SDL_FColor col{std::clamp(c.solid->rgba[0] * c.cx.mult[0] + c.cx.add[0] / 255.0f, 0.0f, 1.0f),
                   std::clamp(c.solid->rgba[1] * c.cx.mult[1] + c.cx.add[1] / 255.0f, 0.0f, 1.0f),
                   std::clamp(c.solid->rgba[2] * c.cx.mult[2] + c.cx.add[2] / 255.0f, 0.0f, 1.0f), a};
    for (const auto& contour : c.solid->contours) {
        if (contour.size() < 3) continue;
        std::vector<SDL_Vertex> verts;
        for (const Point& p : contour) {
            Point q = m.apply(p);
            verts.push_back({{q.x, q.y}, col, {0, 0}});
        }
        std::vector<int> tris = triangulate(contour);
        if (!tris.empty()) SDL_RenderGeometry(r_, nullptr, verts.data(), static_cast<int>(verts.size()), tris.data(), static_cast<int>(tris.size()));
    }
}

namespace {

// Mask shape that is an axis-aligned rectangle in output space? (the common case: 5-point rect contours)
bool rect_of(const DrawCmd& c, const Matrix& view, SDL_Rect* out) {
    if (c.kind != DrawCmd::Kind::Solid || !c.solid || c.solid->contours.size() != 1) return false;
    Matrix m = view * c.m;
    if (!m.axis_aligned()) return false;
    const auto& pts = c.solid->contours[0];
    if (pts.size() < 4 || pts.size() > 5) return false;
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (const Point& p : pts) {
        Point q = m.apply(p);
        minx = std::min(minx, q.x); maxx = std::max(maxx, q.x);
        miny = std::min(miny, q.y); maxy = std::max(maxy, q.y);
    }
    for (const Point& p : pts) {
        Point q = m.apply(p);
        bool xe = std::fabs(q.x - minx) < 0.01f || std::fabs(q.x - maxx) < 0.01f;
        bool ye = std::fabs(q.y - miny) < 0.01f || std::fabs(q.y - maxy) < 0.01f;
        if (!xe || !ye) return false;
    }
    // pixel-centre rule (round, not floor/ceil): rectangles that touch share no pixel, so side-by-side clips tile exactly
    out->x = static_cast<int>(std::lround(minx));
    out->y = static_cast<int>(std::lround(miny));
    out->w = static_cast<int>(std::lround(maxx)) - out->x;
    out->h = static_cast<int>(std::lround(maxy)) - out->y;
    return true;
}

SDL_Rect bounds_of(const DrawCmd& c, const Matrix& view) {
    Matrix m = view * c.m;
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    if (c.solid)
        for (const auto& contour : c.solid->contours)
            for (const Point& p : contour) {
                Point q = m.apply(p);
                minx = std::min(minx, q.x); maxx = std::max(maxx, q.x);
                miny = std::min(miny, q.y); maxy = std::max(maxy, q.y);
            }
    if (minx > maxx) return SDL_Rect{0, 0, 0, 0};
    return SDL_Rect{static_cast<int>(std::floor(minx)), static_cast<int>(std::floor(miny)), static_cast<int>(std::ceil(maxx - minx)) + 1, static_cast<int>(std::ceil(maxy - miny)) + 1};
}

SDL_Rect intersect(SDL_Rect a, SDL_Rect b) {
    int x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y), x1 = std::min(a.x + a.w, b.x + b.w), y1 = std::min(a.y + a.h, b.y + b.h);
    return SDL_Rect{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

}  // namespace

bool SdlRenderer::custom_blend_ok(int w, int h) {
    if (custom_ok_ != -1) return custom_ok_ == 1;
    SDL_Texture* t = make_target(w, h);
    custom_ok_ = (t && SDL_SetTextureBlendMode(t, multiply_by_src_alpha())) ? 1 : 0;
    if (t) { SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND_PREMULTIPLIED); target_pool_.push_back(t); }
    return custom_ok_ == 1;
}

void SdlRenderer::draw(const std::vector<DrawCmd>& cmds, const Matrix& view, int out_w, int out_h) {
    struct MaskFrame { bool scissor; SDL_Texture* mask; SDL_Texture* content; SDL_Texture* prev; bool had_clip; SDL_Rect prev_clip; };
    std::vector<MaskFrame> masks;
    auto matching_apply = [&](size_t begin) {  // index of the MaskApply that closes the MaskBegin at `begin`
        int depth = 0;
        for (size_t j = begin; j < cmds.size(); ++j) {
            if (cmds[j].kind == DrawCmd::Kind::MaskBegin) ++depth;
            else if (cmds[j].kind == DrawCmd::Kind::MaskApply && --depth == 0) return j;
        }
        return cmds.size();
    };
    for (size_t i = 0; i < cmds.size(); ++i) {
        const DrawCmd& c = cmds[i];
        switch (c.kind) {
            case DrawCmd::Kind::Image: draw_image(c, view); break;
            case DrawCmd::Kind::Solid: draw_solid(c, view); break;
            case DrawCmd::Kind::Text: draw_text(c, view); break;
            case DrawCmd::Kind::Raw: draw_raw(c, view); break;
            case DrawCmd::Kind::MaskBegin: {
                size_t j = matching_apply(i);
                SDL_Rect r{};
                bool single_rect = (j == i + 2) && rect_of(cmds[i + 1], view, &r);
                if (single_rect || !custom_blend_ok(out_w, out_h)) {
                    if (!single_rect) {  // no custom blending (software renderer): clip to the mask's bounding box
                        r = SDL_Rect{0, 0, out_w, out_h};
                        bool any = false;
                        for (size_t k = i + 1; k < j; ++k) {
                            if (cmds[k].kind != DrawCmd::Kind::Solid) continue;
                            SDL_Rect b = bounds_of(cmds[k], view);
                            if (!any) { r = b; any = true; }
                            else { int x0 = std::min(r.x, b.x), y0 = std::min(r.y, b.y), x1 = std::max(r.x + r.w, b.x + b.w), y1 = std::max(r.y + r.h, b.y + b.h); r = SDL_Rect{x0, y0, x1 - x0, y1 - y0}; }
                        }
                    }
                    MaskFrame f{true, nullptr, nullptr, nullptr, SDL_RenderClipEnabled(r_), SDL_Rect{0, 0, 0, 0}};
                    if (f.had_clip) SDL_GetRenderClipRect(r_, &f.prev_clip);
                    SDL_Rect nr = f.had_clip ? intersect(r, f.prev_clip) : r;
                    SDL_SetRenderClipRect(r_, &nr);
                    masks.push_back(f);
                    i = j;  // skip the mask shape and the MaskApply
                    break;
                }
                MaskFrame f{false, make_target(out_w, out_h), nullptr, SDL_GetRenderTarget(r_), false, SDL_Rect{0, 0, 0, 0}};
                SDL_SetRenderTarget(r_, f.mask);
                SDL_SetRenderDrawColor(r_, 0, 0, 0, 0);
                SDL_RenderClear(r_);
                masks.push_back(f);
                break;
            }
            case DrawCmd::Kind::MaskApply: {
                if (masks.empty() || masks.back().scissor) break;
                masks.back().content = make_target(out_w, out_h);
                SDL_SetRenderTarget(r_, masks.back().content);
                SDL_SetRenderDrawColor(r_, 0, 0, 0, 0);
                SDL_RenderClear(r_);
                break;
            }
            case DrawCmd::Kind::MaskEnd: {
                if (masks.empty()) break;
                MaskFrame f = masks.back();
                masks.pop_back();
                if (f.scissor) {
                    SDL_SetRenderClipRect(r_, f.had_clip ? &f.prev_clip : nullptr);
                    break;
                }
                if (!f.content) break;
                // content *= mask alpha, then composite onto the previous target
                SDL_SetTextureBlendMode(f.mask, multiply_by_src_alpha());
                SDL_RenderTexture(r_, f.mask, nullptr, nullptr);
                SDL_SetTextureBlendMode(f.mask, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
                SDL_SetRenderTarget(r_, f.prev);
                SDL_RenderTexture(r_, f.content, nullptr, nullptr);
                target_pool_.push_back(f.mask);
                target_pool_.push_back(f.content);
                break;
            }
        }
    }
}

bool SdlRenderer::read_pixels(int* w, int* h, std::vector<unsigned char>* rgba) {
    SDL_Surface* s = SDL_RenderReadPixels(r_, nullptr);
    if (!s) return false;
    SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!c) return false;
    *w = c->w;
    *h = c->h;
    rgba->resize(static_cast<size_t>(c->w) * c->h * 4);
    for (int y = 0; y < c->h; ++y) std::copy_n(static_cast<unsigned char*>(c->pixels) + static_cast<size_t>(y) * c->pitch, static_cast<size_t>(c->w) * 4, rgba->data() + static_cast<size_t>(y) * c->w * 4);
    SDL_DestroySurface(c);
    return true;
}

}  // namespace sbso::engine
