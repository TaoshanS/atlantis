#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/layout.h"
#include "core/resample.h"
#include "core/rng.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void test_rng() {
    sbso::Rng a(42), b(42);
    for (int i = 0; i < 100; ++i) CHECK(a.next() == b.next());
    sbso::Rng c(7);
    for (int i = 0; i < 10; ++i) c.next();
    auto snap = c.state();
    std::uint32_t expect[5];
    for (auto& e : expect) e = c.next();
    sbso::Rng d(0);
    d.set_state(snap);
    for (auto e : expect) CHECK(d.next() == e);  // snapshot/restore reproduces the sequence
    sbso::Rng e2(1);
    for (int i = 0; i < 1000; ++i) { CHECK(e2.below(6) < 6); double u = e2.unit(); CHECK(u >= 0 && u < 1); }
}

static sbso::Image solid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t bl, std::uint8_t a) {
    sbso::Image im; im.w = w; im.h = h; im.rgba.resize(w * h * 4);
    for (int i = 0; i < w * h; ++i) { im.rgba[i*4] = r; im.rgba[i*4+1] = g; im.rgba[i*4+2] = bl; im.rgba[i*4+3] = a; }
    return im;
}

static void test_resample() {
    auto s = solid(8, 8, 200, 100, 50, 255);
    auto up = sbso::resample(s, 24, 24, sbso::Filter::Lanczos3);
    CHECK(up.w == 24);
    for (int i = 0; i < 24 * 24; ++i) {
        CHECK(std::abs(up.rgba[i*4] - 200) <= 1 && std::abs(up.rgba[i*4+1] - 100) <= 1 && up.rgba[i*4+3] == 255);
    }
    auto id = sbso::resample(s, 8, 8, sbso::Filter::Lanczos3);
    CHECK(id.rgba == s.rgba);  // identity scale keeps pixels
    // A white opaque square on a fully transparent black background must not get dark halos.
    sbso::Image sq = solid(16, 16, 0, 0, 0, 0);
    for (int y = 4; y < 12; ++y) for (int x = 4; x < 12; ++x) { auto* p = &sq.rgba[(y*16+x)*4]; p[0]=p[1]=p[2]=255; p[3]=255; }
    auto big = sbso::resample(sq, 48, 48, sbso::Filter::Lanczos3);
    for (int i = 0; i < 48 * 48; ++i) if (big.rgba[i*4+3] > 8) CHECK(big.rgba[i*4] >= 240);
    auto nn = sbso::resample(sq, 32, 32, sbso::Filter::Nearest);
    CHECK(nn.rgba[(8*32+8)*4] == 255 && nn.rgba[0*4+3] == 0);
}

static void test_layout() {
    auto l = sbso::compute_layout(640, 480);
    CHECK(l.logical_w == 640 && l.int_scale == 1);
    l = sbso::compute_layout(2622, 1206);  // iPhone 17 landscape
    CHECK(std::fabs(l.scale - 1206.0f / 480) < 1e-4);
    CHECK(l.logical_w == 1044);
    CHECK(l.int_scale == 2);
    l = sbso::compute_layout(1280, 1280);  // square window: width limits the scale
    CHECK(std::fabs(l.scale - 2.0f) < 1e-4 && l.logical_w == 640);
    l = sbso::compute_layout(4000, 960);  // wider than 2.2:1: the scene stops growing, the rest is side bars
    CHECK(l.logical_w == sbso::kMaxLogicalW && l.ui_left == 0 && l.ui_right == sbso::kMaxLogicalW);
    sbso::Insets in; in.left = 132; in.right = 132;
    l = sbso::compute_layout(2622, 1206, in);
    CHECK(l.ui_left > 50 && l.ui_right < l.logical_w - 50);
    CHECK(sbso::anchor_x(l, sbso::Anchor::Left, 10, 100) == l.ui_left + 10);
    CHECK(std::fabs(sbso::anchor_x(l, sbso::Anchor::Center, 0, 640) - (l.logical_w - 640) * 0.5f) < 1e-3);
    CHECK(std::fabs(sbso::anchor_x(l, sbso::Anchor::Right, 540, 100) - (l.ui_right - 100)) < 1e-3);
}

int main() {
    test_rng(); test_resample(); test_layout();
    std::printf(failures ? "%d FAILED\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}
