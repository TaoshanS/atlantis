// 2D math used by the movie clip runtime (Flash-style affine matrices and colour transforms).
#pragma once
#include <algorithm>
#include <cmath>

namespace sbso::engine {

struct Point { float x = 0, y = 0; };

struct Rect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float w() const { return x1 - x0; }
    float h() const { return y1 - y0; }
    bool empty() const { return x1 <= x0 || y1 <= y0; }
    Rect intersect(const Rect& o) const { return {std::max(x0, o.x0), std::max(y0, o.y0), std::min(x1, o.x1), std::min(y1, o.y1)}; }
};

// | a c tx |   x' = a*x + c*y + tx
// | b d ty |   y' = b*x + d*y + ty
struct Matrix {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;

    Point apply(Point p) const { return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
    // this * o : apply `o` first, then `this` (parent * child).
    Matrix operator*(const Matrix& o) const {
        return {a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d, b * o.c + d * o.d, a * o.tx + c * o.ty + tx, b * o.tx + d * o.ty + ty};
    }
    bool invertible() const { return std::fabs(a * d - b * c) > 1e-12f; }
    Matrix inverse() const {
        float det = a * d - b * c;
        float ia = d / det, ib = -b / det, ic = -c / det, id = a / det;
        return {ia, ib, ic, id, -(ia * tx + ic * ty), -(ib * tx + id * ty)};
    }
    bool axis_aligned() const { return std::fabs(b) < 1e-6f && std::fabs(c) < 1e-6f; }
};

struct ColorTransform {
    float mult[4] = {1, 1, 1, 1};  // r g b a
    float add[4] = {0, 0, 0, 0};   // in 0..255 units
    bool identity() const {
        for (int i = 0; i < 4; ++i)
            if (mult[i] != 1.0f || add[i] != 0.0f) return false;
        return true;
    }
    // parent applied after child: out = parent(child(x))
    ColorTransform concat(const ColorTransform& child) const {
        ColorTransform r;
        for (int i = 0; i < 4; ++i) {
            r.mult[i] = mult[i] * child.mult[i];
            r.add[i] = mult[i] * child.add[i] + add[i];
        }
        return r;
    }
};

}  // namespace sbso::engine
