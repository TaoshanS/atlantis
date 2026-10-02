// Small helpers for building draw commands by hand (UI code that draws bitmaps/text without a clip).
#pragma once
#include <string>

#include "movieclip.h"

namespace sbso::engine {

// A whole bitmap (e.g. the linked BitmapData classes "cards.<symbol>") placed with matrix `m` (image px -> parent space).
inline bool image_cmd(const Library* lib, int image_id, const Matrix& m, DrawCmd* out, const ColorTransform& cx = {}) {
    int w = 0, h = 0;
    if (!lib || !lib->image_size(image_id, &w, &h) || w <= 0) return false;
    out->kind = DrawCmd::Kind::Image;
    out->lib = lib;
    out->image = image_id;
    out->m = m;
    out->src = Rect{0, 0, static_cast<float>(w), static_cast<float>(h)};
    out->cx = cx;
    return true;
}

inline DrawCmd text_cmd(const std::string& font, float size, const float color[4], const std::string& text, const Matrix& m, float x, float y, int align = 0, float box_w = 0) {
    DrawCmd d;
    d.kind = DrawCmd::Kind::Text;
    d.m = m;
    d.run.font = font;
    d.run.size = size;
    for (int i = 0; i < 4; ++i) d.run.color[i] = color[i];
    d.run.text = text;
    d.run.align = align;
    d.run.box = Rect{x, y, x + (box_w > 0 ? box_w : 400.0f), y + size * 1.5f};
    d.run.has_origin = box_w <= 0;
    d.run.ox = x;
    d.run.oy = y + size;  // baseline
    return d;
}

// Left-multiplies `t` onto every drawable command from index `from` on (mask markers carry no matrix).
inline void transform_all(std::vector<DrawCmd>& cmds, size_t from, const Matrix& t) {
    for (size_t i = from; i < cmds.size(); ++i)
        if (cmds[i].kind != DrawCmd::Kind::MaskBegin && cmds[i].kind != DrawCmd::Kind::MaskApply && cmds[i].kind != DrawCmd::Kind::MaskEnd) cmds[i].m = t * cmds[i].m;
}

inline void translate_all(std::vector<DrawCmd>& cmds, size_t from, float dx, float dy) {
    Matrix t{1, 0, 0, 1, dx, dy};
    for (size_t i = from; i < cmds.size(); ++i)
        if (cmds[i].kind != DrawCmd::Kind::MaskBegin && cmds[i].kind != DrawCmd::Kind::MaskApply && cmds[i].kind != DrawCmd::Kind::MaskEnd) cmds[i].m = t * cmds[i].m;
}

}  // namespace sbso::engine
