// Character library for one SWF, loaded from the extractor's characters/<swf>.json.
#pragma once
#include <map>
#include <string>
#include <variant>
#include <vector>

#include "geom.h"

namespace sbso::engine {

struct ImageQuad {
    int image = -1;          // bitmap character id
    Matrix to_shape;         // image pixel space -> shape local space
    Rect clip;               // region of the shape covered by this fill (shape local space)
    bool smooth = true;
};

struct SolidPath {           // vector fill (rare): flattened polygon(s) in shape space
    float rgba[4] = {0, 0, 0, 1};
    std::vector<std::vector<Point>> contours;
};

struct ShapeDef {
    Rect bounds;
    std::vector<ImageQuad> quads;
    std::vector<SolidPath> solids;
};

using Value = std::variant<std::monostate, int, double, bool, std::string>;

struct FrameCall {
    std::string call, recv;
    std::vector<Value> args;
};

struct Op {
    int depth = 0;
    bool remove = false;
    int id = -1;             // character to place (-1: keep)
    bool move = false;
    bool has_matrix = false, has_cx = false;
    Matrix m;
    ColorTransform cx;
    int clip_depth = -1;
    std::string name;
    int sound = -1;          // StartSound
};

struct Frame {
    std::vector<Op> ops;
    std::string label;
    std::vector<FrameCall> calls;  // recovered frame script
};

struct ButtonRecord {
    int id = -1, depth = 0;
    Matrix m;
    ColorTransform cx;
    bool up = false, over = false, down = false, hit = false;
};

struct TextDef {
    Rect bounds;
    std::string text, variable, font_class;
    float size = 12;
    float color[4] = {0, 0, 0, 1};
    int font = -1;
    int align = 0;  // 0 left 1 right 2 centre 3 justify
    bool html = false, multiline = false, wrap = false, autosize = false, readonly = true;
};

struct FontDef {
    std::string name;
    int glyphs = 0;
    bool bold = false, italic = false;
    std::vector<int> codes;  // glyph index -> UTF-16 code unit
};

struct StaticRun {
    int font = -1;
    float size = 12;
    float color[4] = {0, 0, 0, 1};
    float x = 0, y = 0;  // pen origin (baseline), relative to the text object
    std::string text;    // UTF-8
    std::vector<float> advances;
};

struct StaticText {
    Rect bounds;
    Matrix m;
    std::vector<StaticRun> runs;
};

class Library {
public:
    // `characters_json` is characters/<swf>.json; `images_dir` is images/<swf>/ (used only to read sizes).
    bool load(const std::string& characters_json, const std::string& images_dir, std::string* err);

    const ShapeDef* shape(int id) const { auto it = shapes_.find(id); return it == shapes_.end() ? nullptr : &it->second; }
    const std::vector<Frame>* sprite(int id) const { auto it = sprites_.find(id); return it == sprites_.end() ? nullptr : &it->second; }
    const std::vector<ButtonRecord>* button(int id) const { auto it = buttons_.find(id); return it == buttons_.end() ? nullptr : &it->second; }
    const TextDef* text(int id) const { auto it = texts_.find(id); return it == texts_.end() ? nullptr : &it->second; }
    const StaticText* static_text(int id) const { auto it = statics_.find(id); return it == statics_.end() ? nullptr : &it->second; }
    const FontDef* font(int id) const { auto it = fonts_.find(id); return it == fonts_.end() ? nullptr : &it->second; }
    int symbol_id(const std::string& cls) const { auto it = by_name_.find(cls); return it == by_name_.end() ? -1 : it->second; }
    const std::string& symbol_name(int id) const;
    const std::vector<Frame>& root() const { return root_; }
    const std::vector<Frame>* root_frames() const { return &root_; }
    float frame_rate() const { return frame_rate_; }
    Rect stage() const { return stage_; }
    int solids_skipped() const { return solids_; }
    bool image_size(int id, int* w, int* h) const;  // reads the PNG header on first use
    const std::string& name() const { return name_; }
    // Path of the extracted PNG for bitmap `id`: <images_root>/<swf key>/<id>.png
    std::string image_file(const std::string& images_root, int id) const;

private:
    std::string name_;
    std::map<int, ShapeDef> shapes_;
    std::map<int, std::vector<Frame>> sprites_;
    std::map<int, std::vector<ButtonRecord>> buttons_;
    std::map<int, TextDef> texts_;
    std::map<int, FontDef> fonts_;
    std::map<int, StaticText> statics_;
    std::map<std::string, int> by_name_;
    std::map<int, std::string> names_;
    std::string images_dir_;
    mutable std::map<int, std::pair<int, int>> image_sizes_;
    std::vector<Frame> root_;
    float frame_rate_ = 30;
    Rect stage_{0, 0, 640, 480};
    int solids_ = 0;
};

}  // namespace sbso::engine
