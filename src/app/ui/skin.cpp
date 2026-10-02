#include "skin.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "engine/draw_util.h"

namespace sbso::app {
using engine::Matrix;
using engine::Rect;

namespace {
constexpr int kBracketTop = 4, kBracketBottom = 3;  // titlescreen: the hover brackets (14x16, mirrored for the right side)
constexpr int kPanelImage = 1;  // menu_assets: the empty bamboo panel used by the in-game menu (374x316)
constexpr float kC = 30;        // frame slice: corner size in source pixels
constexpr float kGreen[3] = {168 / 255.0f, 183 / 255.0f, 88 / 255.0f};
constexpr float kPlate[3] = {124 / 255.0f, 138 / 255.0f, 59 / 255.0f};
constexpr int kGreenButton = 11, kRedButton = 14;  // menu_assets: OPTIONS / QUIT buttons of the in-game menu (220x53)
constexpr float kButtonBodyH = 49;  // rows of the bitmap above the soft drop shadow
constexpr float kButtonCap = 8;     // rounded end caps, kept unstretched
constexpr float kButtonPlainX0 = 20, kButtonPlainX1 = 60;  // columns with no lettering on both bitmaps
// fallback button colours
constexpr float kBorder[3] = {16 / 255.0f, 55 / 255.0f, 35 / 255.0f};
constexpr float kBody[3] = {151 / 255.0f, 211 / 255.0f, 86 / 255.0f};
}  // namespace

engine::SolidPath* Skin::add(const engine::SolidPath& p) {
    shapes_.push_back(p);
    return &shapes_.back();
}

void Skin::solid(std::vector<engine::DrawCmd>& out, const engine::SolidPath& p) {
    engine::DrawCmd d;
    d.kind = engine::DrawCmd::Kind::Solid;
    d.solid = add(p);
    out.push_back(d);
}

Rect Skin::plate_of(const Rect& r) { return Rect{r.x0 + 35, r.y0 + 42, r.x1 - 36, r.y1 - 44}; }

Rect Skin::panel(std::vector<engine::DrawCmd>& out, const Rect& r) {
    solid(out, engine::round_rect(r.x0 + 18, r.y0 + 20, r.w() - 36, r.h() - 40, 0, kGreen[0], kGreen[1], kGreen[2]));
    Rect pl = plate_of(r);
    solid(out, engine::round_rect(pl.x0, pl.y0, pl.w(), pl.h(), 9, kPlate[0], kPlate[1], kPlate[2]));
    const engine::Library* lib = app_.assets().library("menu_assets");
    int iw = 0, ih = 0;
    if (lib && lib->image_size(kPanelImage, &iw, &ih)) {
        // nine-slice of the frame ring: corners as they are, edges stretched along their axis
        auto slice = [&](float sx0, float sy0, float sx1, float sy1, float dx0, float dy0, float dx1, float dy1) {
            engine::DrawCmd d;
            d.kind = engine::DrawCmd::Kind::Image;
            d.lib = lib;
            d.image = kPanelImage;
            d.src = Rect{sx0, sy0, sx1, sy1};
            float sx = (dx1 - dx0) / (sx1 - sx0), sy = (dy1 - dy0) / (sy1 - sy0);
            d.m = Matrix{sx, 0, 0, sy, dx0 - sx0 * sx, dy0 - sy0 * sy};
            out.push_back(d);
        };
        const float W = static_cast<float>(iw), H = static_cast<float>(ih);
        slice(kC, 0, W - kC, kC, r.x0 + kC, r.y0, r.x1 - kC, r.y0 + kC);            // top
        slice(kC, H - kC, W - kC, H, r.x0 + kC, r.y1 - kC, r.x1 - kC, r.y1);        // bottom
        slice(0, kC, kC, H - kC, r.x0, r.y0 + kC, r.x0 + kC, r.y1 - kC);            // left
        slice(W - kC, kC, W, H - kC, r.x1 - kC, r.y0 + kC, r.x1, r.y1 - kC);        // right
        slice(0, 0, kC, kC, r.x0, r.y0, r.x0 + kC, r.y0 + kC);                       // corners
        slice(W - kC, 0, W, kC, r.x1 - kC, r.y0, r.x1, r.y0 + kC);
        slice(0, H - kC, kC, H, r.x0, r.y1 - kC, r.x0 + kC, r.y1);
        slice(W - kC, H - kC, W, H, r.x1 - kC, r.y1 - kC, r.x1, r.y1);
    }
    return pl;
}

void Skin::plate(std::vector<engine::DrawCmd>& out, const Rect& r, float radius, float shade) {
    float k = 1.0f - shade;
    solid(out, engine::round_rect(r.x0, r.y0, r.w(), r.h(), radius, std::clamp(kPlate[0] * k, 0.0f, 1.0f), std::clamp(kPlate[1] * k, 0.0f, 1.0f), std::clamp(kPlate[2] * k, 0.0f, 1.0f)));
}

void Skin::text(std::vector<engine::DrawCmd>& out, const std::string& s, float x, float y, float size, int align, float box_w, bool dim) {
    // original menu lettering: white fill, dark green outline, a little drop shadow
    static const float kOutline[4] = {16 / 255.0f, 55 / 255.0f, 35 / 255.0f, 1.0f};
    static const float kShade[4] = {0, 0, 0, 0.25f};
    const float white[4] = {1, 1, 1, dim ? 0.75f : 1.0f};
    const float r = size >= 20 ? 1.1f : (size >= 14 ? 0.8f : 0.55f);
    out.push_back(engine::text_cmd("tikimagic", size, kShade, s, Matrix{}, x, y + r + 1.0f, align, box_w));
    for (int i = 0; i < 8; ++i) {
        float a = i * 0.785398f;
        out.push_back(engine::text_cmd("tikimagic", size, kOutline, s, Matrix{}, x + std::cos(a) * r, y + std::sin(a) * r, align, box_w));
    }
    out.push_back(engine::text_cmd("tikimagic", size, white, s, Matrix{}, x, y, align, box_w));
}

void Skin::button(std::vector<engine::DrawCmd>& out, const Rect& r, const std::string& label, bool hover, bool enabled, bool red, float size,
                  std::vector<engine::DrawCmd>* brackets_out) {
    const engine::Library* lib = app_.assets().library("menu_assets");
    const int img = red ? kRedButton : kGreenButton;
    int iw = 0, ih = 0;
    if (lib && lib->image_size(img, &iw, &ih)) {
        // The original button bitmap (OPTIONS / QUIT of the in-game menu, 220x53 with its drop shadow): scaled so its body (49 px) fits
        // the button height, end caps kept, the middle stretched from a column range with no lettering.
        const float k = r.h() / kButtonBodyH, cap = kButtonCap;
        engine::ColorTransform cx;
        if (!enabled) {  // greyed like a disabled original button
            for (int i = 0; i < 3; ++i) { cx.mult[i] = 0.45f; cx.add[i] = 70; }
        }
        auto slice = [&](float sx0, float sx1, float dx0, float dx1) {
            engine::DrawCmd d;
            d.kind = engine::DrawCmd::Kind::Image;
            d.lib = lib;
            d.image = img;
            d.src = Rect{sx0, 0, sx1, static_cast<float>(ih)};
            float sx = (dx1 - dx0) / (sx1 - sx0);
            d.m = Matrix{sx, 0, 0, k, dx0 - sx0 * sx, r.y0};
            d.cx = cx;
            out.push_back(d);
        };
        const float W = static_cast<float>(iw), c = cap * k;
        slice(kButtonPlainX0, kButtonPlainX1, r.x0 + c - 1, r.x1 - c + 1);  // the middle runs under the caps: no hairline seam
        slice(0, cap, r.x0, r.x0 + c);
        slice(W - cap, W, r.x1 - c, r.x1);
    } else {  // no assets (tests): a flat stand-in
        solid(out, engine::round_rect(r.x0, r.y0, r.w(), r.h(), 4, kBorder[0], kBorder[1], kBorder[2]));
        solid(out, engine::round_rect(r.x0 + 2, r.y0 + 2, r.w() - 4, r.h() - 4, 3, kBody[0], kBody[1], kBody[2]));
    }
    // lettering of the original button bitmaps: white with a soft halo of the button colour darkened (no dark outline)
    float ty = r.y0 + (r.h() - 2) / 2 - size * 0.5f;
    const float halo[4] = {red ? 120 / 255.0f : 100 / 255.0f, red ? 0.0f : 149 / 255.0f, red ? 0.0f : 57 / 255.0f, enabled ? 1.0f : 0.6f};
    const float glow[4] = {halo[0], halo[1], halo[2], 0.35f};
    const float white[4] = {1, 1, 1, enabled ? 1.0f : 0.7f};
    const float rr = std::max(0.6f, size * 0.05f);
    for (int ring = 0; ring < 2; ++ring)
        for (int i = 0; i < 8; ++i) {
            float a = i * 0.785398f, d = ring ? rr * 1.9f : rr;
            out.push_back(engine::text_cmd("tikimagic", size, ring ? glow : halo, label, Matrix{}, r.x0 + std::cos(a) * d, ty + std::sin(a) * d, 2, r.w()));
        }
    out.push_back(engine::text_cmd("tikimagic", size, white, label, Matrix{}, r.x0, ty, 2, r.w()));
    if (hover && enabled) {
        int key = static_cast<int>(r.x0) * 7919 + static_cast<int>(r.y0) + 1;
        cur_hover_ = key;
        if (key != prev_hover_) { app_.sound().play_sound("button_rollover", false, -1); hover_since_ns_ = SDL_GetTicksNS(); }
        // the original hover (titlescreen sprite 7): four white corner brackets around the button that bounce out ~2 px
        if (const engine::Library* lib = app_.assets().library("titlescreen")) {
            int bw = 0, bh = 0;
            if (lib->image_size(kBracketTop, &bw, &bh)) {
                // the sprite holds 5 frames at rest and 5 frames 2.2 px out, looping (32 fps title movie)
                double secs = static_cast<double>(SDL_GetTicksNS() - hover_since_ns_) * 1e-9;
                // titlescreen button 8: the 138x64 bracket sprite sits at (-5.85, -8.1) on a 126x51 button, i.e. clear of its edges.
                // Buttons much smaller than the originals get slightly smaller brackets (and gaps) so the corners do not collide.
                float k = std::min(1.0f, (r.h() + 13.15f) / 40.0f), o = (static_cast<long>(secs * 32.0) % 10) >= 5 ? 2.2f : 0.0f;
                float h = bh * k;
                float gl = 5.85f * k + o, gr = 6.15f * k + o, gt = 8.1f * k + o, gb = 5.05f * k + o;
                auto corner = [&](int image, float x, float y, float sx, float sy) {
                    engine::DrawCmd d;
                    d.kind = engine::DrawCmd::Kind::Image;
                    d.lib = lib;
                    d.image = image;
                    d.src = Rect{0, 0, static_cast<float>(bw), static_cast<float>(bh)};
                    d.m = Matrix{sx * k, 0, 0, sy * k, x, y};
                    (brackets_out ? *brackets_out : out).push_back(d);
                };
                corner(kBracketTop, r.x0 - gl, r.y0 - gt, 1, 1);
                corner(kBracketBottom, r.x0 - gl, r.y1 + gb - h, 1, 1);
                corner(kBracketTop, r.x1 + gr, r.y0 - gt, -1, 1);
                corner(kBracketBottom, r.x1 + gr, r.y1 + gb - h, -1, 1);
            }
        }
    }
}

}  // namespace sbso::app
