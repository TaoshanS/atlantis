// MovieClip runtime: timelines, display lists and frame scripts of the original SWF content.
#pragma once
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

#include "library.h"

namespace sbso::engine {

class MovieClip;

std::string normalize_font_name(const std::string& n);  // lower case, spaces removed

// CPU-generated bitmap (baked tile layers, rasterised text). Straight-alpha RGBA8.
struct RawBitmap {
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
};

struct TextRun {
    std::string font;  // normalised font name (lower case, no spaces); may be empty
    float size = 12;
    float color[4] = {0, 0, 0, 1};
    Rect box;          // text box in the object's local space
    int align = 0;     // 0 left, 1 right, 2 centre
    bool wrap = false, multiline = false;
    std::string text;  // UTF-8
    std::vector<float> advances;  // optional per-character advances (static text)
    bool has_origin = false;      // static runs: pen origin (baseline) instead of a box
    float ox = 0, oy = 0;
    float line_height = 0;        // >0: fixed line pitch (local px) instead of the font metrics
};

// What a draw command produces. A renderer turns these into textured quads.
struct DrawCmd {
    enum class Kind { Image, Solid, Text, MaskBegin, MaskApply, MaskEnd, Raw } kind = Kind::Image;
    std::shared_ptr<const RawBitmap> raw;  // Raw: m maps raw pixels to the parent space, src is the full bitmap
    int image = -1;          // Image: bitmap character id (in the clip's library)
    const Library* lib = nullptr;
    Matrix m;                // Image: image pixel space -> root space.  Text: text box space -> root
    Rect src;                // Image: source rectangle in image pixels
    ColorTransform cx;
    const SolidPath* solid = nullptr;  // Solid
    TextRun run;                       // Text
    bool smooth = true;

    DrawCmd() = default;
    explicit DrawCmd(Kind k) : kind(k) {}
};

// Callbacks into the game for things the original timelines do.
class ClipHost {
public:
    virtual ~ClipHost() = default;
    virtual void play_sound(const std::string& name, bool loop, int loops) { (void)name; (void)loop; (void)loops; }
    virtual void dispatch_event(MovieClip& from, const std::string& name) { (void)from; (void)name; }
    virtual void unhandled_call(MovieClip& clip, const FrameCall& call) { (void)clip; (void)call; }
};

// A button under the pointer (DefineButton characters are the interactive elements of the original).
struct ButtonHit {
    MovieClip* owner = nullptr;  // the clip whose display list holds the button
    int depth = 0;
    std::string name;             // instance name of the button placement (may be empty)
    std::vector<Rect> hit_rects;  // hit area in root space (axis-aligned boxes)
    // Touch screens get a margin of `slop` logical pixels around every hit area (fingers are less precise than a mouse; many original
    // buttons are smaller than a fingertip). exact() ignores it, so callers can prefer a direct hit over a near one.
    static inline float slop = 0;
    bool exact(Point p) const { return within(p, 0); }
    bool contains(Point p) const { return within(p, slop); }
    bool within(Point p, float m) const {
        for (const Rect& r : hit_rects)
            if (p.x >= r.x0 - m && p.x < r.x1 + m && p.y >= r.y0 - m && p.y < r.y1 + m) return true;
        return false;
    }
};

class MovieClip {
public:
    // Creates an instance of sprite `char_id` of `lib` (use root() for the SWF main timeline).
    static std::unique_ptr<MovieClip> create(const Library* lib, int char_id, ClipHost* host);
    static std::unique_ptr<MovieClip> create_root(const Library* lib, ClipHost* host);
    // Instantiates any character (sprite, button, shape, text): non-sprites are wrapped in a one-frame clip.
    static std::unique_ptr<MovieClip> create_any(const Library* lib, int char_id, ClipHost* host);

    int char_id() const { return char_id_; }
    const std::string& name() const { return name_; }
    int current_frame() const { return cur_; }          // 0-based
    int total_frames() const { return static_cast<int>(frames_->size()); }
    bool playing() const { return playing_; }
    int frame_of_label(const std::string& label) const;  // 0-based or -1

    void play() { playing_ = true; }
    void stop() { playing_ = false; }
    void goto_and_play(int frame1) { goto_frame(frame1 - 1, true); }
    void goto_and_stop(int frame1) { goto_frame(frame1 - 1, false); }
    bool goto_label_and_play(const std::string& l) { int f = frame_of_label(l); if (f < 0) return false; goto_frame(f, true); return true; }
    bool goto_label_and_stop(const std::string& l) { int f = frame_of_label(l); if (f < 0) return false; goto_frame(f, false); return true; }

    // One frame of the player: advance the playhead (if playing) and the children.
    void tick();

    MovieClip* child(const std::string& instance_name);  // direct children only
    MovieClip* find(const std::string& path);              // "a.b.c"
    void set_text(const std::string& instance_name, const std::string& text);
    // Direct-child property overrides (the AS code assigns x/y/alpha/visible on named children).
    bool set_child_xy(const std::string& instance_name, float x, float y);
    // The TextRun a text child would be drawn with (current text, wrapped when set from code); false if there is no such text child.
    bool text_run(const std::string& instance_name, TextRun* out) const;
    // Draw the placement at `depth` stretched horizontally to span [x0, x1] in this clip's coordinates (full-width bands on wide screens). x1 <= x0 clears it.
    void stretch_depth_x(int depth, float x0, float x1);
    void set_depth_hidden(int depth, bool hidden);   // skip drawing the placement at `depth` (e.g. a baked panel replaced by a resizable one)
    // Children named `name` keep their instance (playback state, contents) when the timeline removes and re-places them at the same
    // frame, instead of restarting as a fresh copy. Used to make the outro of a dialogue panel continue instead of blanking.
    void keep_instance(const std::string& name) { keep_names_.insert(name); }
    // The sprites of a button's over/down states play once and stay on their last frame (the AS adds a stop() script there:
    // LevelSelectScreen does it for the play, arrow and chest buttons). Applies to the child called `name`.
    void stop_button_over_clips_at_end(const std::string& name);
    void set_text_line_height(const std::string& instance_name, float h);  // fixed line pitch for a text child (0 = font metrics)
    bool set_child_tint(const std::string& instance_name, int r, int g, int b);  // flat colour via a colour transform
    bool set_child_alpha(const std::string& instance_name, float a);
    // addChild: extra clips drawn above the timeline content of this clip (code-built children such as the maps on the level select).
    MovieClip* add_child_clip(std::unique_ptr<MovieClip> c, const Matrix& m = Matrix{});
    bool remove_child_clip(MovieClip* c);
    // Same, for a clip owned elsewhere (cached stage maps): the caller keeps it alive while attached.
    MovieClip* attach_child_clip(MovieClip* c, const Matrix& m = Matrix{});
    bool set_child_clip_matrix(MovieClip* c, const Matrix& m);
    MovieClip* replace_child_clip(MovieClip* old_clip, std::unique_ptr<MovieClip> c, const Matrix& m);  // keeps the z order
    void clear_child_clips() { overlays_.clear(); }
    void set_name(const std::string& n) { name_ = n; }
    struct NamedChild { std::string name; Point pos; };
    std::vector<NamedChild> named_children() const;  // direct children with an instance name, display-list order
    std::string current_label() const;  // label of the last labelled frame at or before the current one
    bool set_child_visible(const std::string& instance_name, bool visible);  // any child, buttons included
    // The AS pattern `child.removeChildAt(0); child.addChild(x)`: draw `content` instead of the named child's lowest display object.
    bool replace_child_content(const std::string& instance_name, std::unique_ptr<MovieClip> content);
    bool get_child_xy(const std::string& instance_name, float* x, float* y) const;
    bool child_bounds(const std::string& instance_name, Rect* out) const;  // in this clip's space
    // World matrix (relative to this clip) of the descendant at "a.b.c".
    bool path_matrix(const std::string& dotted, Matrix* out) const;
    bool set_child_scale_x(const std::string& instance_name, float sx);
    // Draw (and hit-test) the children named `instance_name` scaled by `s` around their centre; kept across timeline re-placements
    // (phones: finger-sized CLOSE buttons). s = 1 restores them.
    void enlarge_named(const std::string& instance_name, float s) {  // enlarge_child here and in every nested clip
        enlarge_child(instance_name, s);
        for (auto& [d, c] : children_) if (c.clip) c.clip->enlarge_named(instance_name, s);
    }
    void enlarge_child(const std::string& instance_name, float s) { if (s == 1.0f) enlarge_.erase(instance_name); else enlarge_[instance_name] = s; }

    void set_matrix(const Matrix& m) { matrix_ = m; }
    const Matrix& matrix() const { return matrix_; }
    void set_visible(bool v) { visible_ = v; }
    bool visible() const { return visible_; }
    MovieClip* parent() const { return parent_; }
    // Bounds of the drawn content in this clip's own space (shapes, nested clips, text boxes). Empty if nothing is drawn.
    Rect bounds() const;

    // Interactive buttons below this clip (paint order, topmost last), with hit areas in root space.
    void collect_buttons(std::vector<ButtonHit>& out, const Matrix& root_m);
    enum class ButtonState { Up, Over, Down };
    void set_button_state(int depth, ButtonState s);
    static bool record_in_state(const ButtonRecord& r, ButtonState st, const std::vector<ButtonRecord>& all);
    // True if this clip or any ancestor has the instance name `n` (how AS listeners on a named sprite see clicks).
    bool in_named(const std::string& n) const;

    // Appends the draw commands for this clip and its children. `root_m` maps this clip's space to root space.
    void collect(std::vector<DrawCmd>& out, const Matrix& root_m, const ColorTransform& cx) const;

private:
    struct Child {
        int depth = 0, char_id = -1, placed_frame = 0;
        Matrix m;
        ColorTransform cx;
        std::string name;
        int clip_depth = -1;
        std::unique_ptr<MovieClip> clip;  // sprites only
        std::string text;                 // text fields
        bool has_text_override = false;
        float text_line_height = 0;
        ButtonState button = ButtonState::Up;
        bool hidden = false;  // set_child_visible(false)
        std::vector<std::unique_ptr<MovieClip>> button_clips;  // sprite records of a button, same order as the records
    };

    MovieClip() = default;
    void init(const Library* lib, const std::vector<Frame>* frames, int char_id, ClipHost* host);
    void goto_frame(int frame0, bool play);
    void enter_frame(int frame0, bool sequential);
    void apply_ops(int frame0, bool place_new);
    void run_scripts(int frame0);
    Child make_child(const Op& op, int frame0);
    void rebuild_to(int frame0);
    void draw_placement(std::vector<DrawCmd>& out, const Child& c, const Matrix& w, const ColorTransform& ccx) const;
    TextRun text_run_of(const Child& c, const TextDef& t) const;

    const Library* lib_ = nullptr;
    const std::vector<Frame>* frames_ = nullptr;
    ClipHost* host_ = nullptr;
    MovieClip* parent_ = nullptr;
    int char_id_ = -1;
    std::string name_;
    int cur_ = -1;
    bool playing_ = true;
    bool visible_ = true;
    bool stop_at_end_ = false;      // stop on the last frame instead of looping (set by stop_button_over_clips_at_end)
    std::map<int, std::pair<float, float>> x_stretch_;
    std::map<std::string, float> enlarge_;  // enlarge_child
    Matrix child_matrix(const Child& c) const;
    Rect child_bounds(const Child& c) const;  // in this clip's space  // depth -> [x0, x1] (stretch_depth_x)
    std::set<std::string> keep_names_;
    std::map<std::string, std::unique_ptr<MovieClip>> stash_;  // instances taken off by a remove, waiting for the re-placement (keep_instance)
    Matrix matrix_;
    struct Overlay { std::unique_ptr<MovieClip> own; MovieClip* clip = nullptr; Matrix m; };
    std::vector<Overlay> overlays_;
    std::map<int, Child> children_;  // depth -> child, ascending depth is paint order
    int scripts_guard_ = 0;
    std::unique_ptr<MovieClip> content_override_;  // see replace_child_content
    std::shared_ptr<std::vector<Frame>> owned_frames_;  // synthetic frames for wrapper clips
};

}  // namespace sbso::engine
