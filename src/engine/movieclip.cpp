#include "movieclip.h"

#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <cctype>

namespace sbso::engine {
std::string normalize_font_name(const std::string& n) {
    std::string out;
    for (char c : n)
        if (c != ' ' && c != '\0') out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

namespace {

const std::vector<Frame> kNoFrames;

}  // namespace

std::unique_ptr<MovieClip> MovieClip::create(const Library* lib, int char_id, ClipHost* host) {
    const std::vector<Frame>* f = lib->sprite(char_id);
    std::unique_ptr<MovieClip> c(new MovieClip());
    c->init(lib, f ? f : &kNoFrames, char_id, host);
    return c;
}

std::unique_ptr<MovieClip> MovieClip::create_any(const Library* lib, int char_id, ClipHost* host) {
    if (lib->sprite(char_id)) return create(lib, char_id, host);
    std::unique_ptr<MovieClip> c(new MovieClip());
    c->owned_frames_ = std::make_shared<std::vector<Frame>>(1);
    Op op;
    op.depth = 1;
    op.id = char_id;
    op.has_matrix = true;
    (*c->owned_frames_)[0].ops.push_back(op);
    c->init(lib, c->owned_frames_.get(), char_id, host);
    return c;
}

std::unique_ptr<MovieClip> MovieClip::create_root(const Library* lib, ClipHost* host) {
    std::unique_ptr<MovieClip> c(new MovieClip());
    c->init(lib, lib->root_frames(), 0, host);
    return c;
}

void MovieClip::init(const Library* lib, const std::vector<Frame>* frames, int char_id, ClipHost* host) {
    lib_ = lib;
    frames_ = frames;
    char_id_ = char_id;
    host_ = host;
    if (!frames_->empty()) enter_frame(0, true);
}

std::vector<MovieClip::NamedChild> MovieClip::named_children() const {
    std::vector<NamedChild> r;
    for (const auto& [d, c] : children_)
        if (!c.name.empty()) r.push_back({c.name, {c.m.tx, c.m.ty}});
    return r;
}

std::string MovieClip::current_label() const {
    for (int i = cur_; i >= 0; --i)
        if (!(*frames_)[i].label.empty()) return (*frames_)[i].label;
    return {};
}

MovieClip* MovieClip::add_child_clip(std::unique_ptr<MovieClip> c, const Matrix& m) {
    c->parent_ = this;
    MovieClip* raw = c.get();
    overlays_.push_back({std::move(c), raw, m});
    return raw;
}

MovieClip* MovieClip::attach_child_clip(MovieClip* c, const Matrix& m) {
    c->parent_ = this;
    overlays_.push_back({nullptr, c, m});
    return c;
}

MovieClip* MovieClip::replace_child_clip(MovieClip* old_clip, std::unique_ptr<MovieClip> c, const Matrix& m) {
    for (auto& o : overlays_)
        if (o.clip == old_clip) {
            c->parent_ = this;
            o.clip = c.get();
            o.own = std::move(c);
            o.m = m;
            return o.clip;
        }
    return add_child_clip(std::move(c), m);
}

bool MovieClip::set_child_clip_matrix(MovieClip* c, const Matrix& m) {
    for (auto& o : overlays_)
        if (o.clip == c) { o.m = m; return true; }
    return false;
}

bool MovieClip::remove_child_clip(MovieClip* c) {
    for (auto it = overlays_.begin(); it != overlays_.end(); ++it)
        if (it->clip == c) { if (!it->own) c->parent_ = nullptr; overlays_.erase(it); return true; }
    return false;
}

int MovieClip::frame_of_label(const std::string& label) const {
    for (size_t i = 0; i < frames_->size(); ++i)
        if ((*frames_)[i].label == label) return static_cast<int>(i);
    return -1;
}

MovieClip::Child MovieClip::make_child(const Op& op, int frame0) {
    Child c;
    c.depth = op.depth;
    c.char_id = op.id;
    c.placed_frame = frame0;
    if (op.has_matrix) c.m = op.m;
    if (op.has_cx) c.cx = op.cx;
    c.name = op.name;
    c.clip_depth = op.clip_depth;
    if (lib_->sprite(op.id)) {
        c.clip = MovieClip::create(lib_, op.id, host_);
        c.clip->parent_ = this;
        c.clip->name_ = op.name;
    } else if (const TextDef* t = lib_->text(op.id)) {
        c.text = t->text;
    } else if (const auto* recs = lib_->button(op.id)) {
        for (const ButtonRecord& r : *recs) {
            if (lib_->sprite(r.id)) {
                auto sc = MovieClip::create(lib_, r.id, host_);
                sc->parent_ = this;
                c.button_clips.push_back(std::move(sc));
            } else {
                c.button_clips.push_back(nullptr);
            }
        }
    }
    return c;
}

void MovieClip::apply_ops(int frame0, bool) {
    for (const Op& op : (*frames_)[frame0].ops) {
        if (op.sound >= 0) continue;
        if (op.remove) {
            auto rm = children_.find(op.depth);
            if (rm != children_.end() && rm->second.clip && !rm->second.name.empty() && keep_names_.count(rm->second.name))
                stash_[rm->second.name] = std::move(rm->second.clip);
            children_.erase(op.depth);
            continue;
        }
        auto it = children_.find(op.depth);
        if (op.move && it != children_.end()) {
            Child& c = it->second;
            if (op.id >= 0 && op.id != c.char_id) {
                Op full = op;
                if (!full.has_matrix) { full.has_matrix = true; full.m = c.m; }
                if (!full.has_cx) { full.has_cx = true; full.cx = c.cx; }
                if (full.name.empty()) full.name = c.name;
                if (full.clip_depth < 0) full.clip_depth = c.clip_depth;
                Child nc = make_child(full, frame0);
                it->second = std::move(nc);
            } else {
                if (op.has_matrix) c.m = op.m;
                if (op.has_cx) c.cx = op.cx;
                if (!op.name.empty()) { c.name = op.name; if (c.clip) c.clip->name_ = op.name; }
                if (op.clip_depth >= 0) c.clip_depth = op.clip_depth;
            }
        } else if (op.id >= 0) {
            children_[op.depth] = make_child(op, frame0);
            Child& nc = children_[op.depth];
            auto kept = nc.name.empty() ? stash_.end() : stash_.find(nc.name);
            if (kept != stash_.end() && nc.clip) {  // keep_instance: carry on with the previous instance
                nc.clip = std::move(kept->second);
                nc.clip->parent_ = this;
                stash_.erase(kept);
            }
        }
    }
}

void MovieClip::rebuild_to(int frame0) {
    struct State { int id; Matrix m; ColorTransform cx; std::string name; int clip_depth; int placed; };
    std::map<int, State> st;
    for (int f = 0; f <= frame0; ++f) {
        for (const Op& op : (*frames_)[f].ops) {
            if (op.sound >= 0) continue;
            if (op.remove) { st.erase(op.depth); continue; }
            auto it = st.find(op.depth);
            if (op.move && it != st.end()) {
                State& s = it->second;
                if (op.id >= 0 && op.id != s.id) { s.id = op.id; s.placed = f; }
                if (op.has_matrix) s.m = op.m;
                if (op.has_cx) s.cx = op.cx;
                if (!op.name.empty()) s.name = op.name;
                if (op.clip_depth >= 0) s.clip_depth = op.clip_depth;
            } else if (op.id >= 0) {
                State s{op.id, op.has_matrix ? op.m : Matrix{}, op.has_cx ? op.cx : ColorTransform{}, op.name, op.clip_depth, f};
                st[op.depth] = s;
            }
        }
    }
    std::map<int, Child> next;
    for (auto& [depth, s] : st) {
        auto it = children_.find(depth);
        if (it != children_.end() && it->second.char_id == s.id && it->second.placed_frame == s.placed) {
            Child c = std::move(it->second);
            c.m = s.m; c.cx = s.cx; c.name = s.name; c.clip_depth = s.clip_depth;
            if (c.clip) c.clip->name_ = s.name;
            next[depth] = std::move(c);
        } else {
            Op op;
            op.depth = depth; op.id = s.id; op.has_matrix = true; op.m = s.m; op.has_cx = true; op.cx = s.cx; op.name = s.name; op.clip_depth = s.clip_depth;
            next[depth] = make_child(op, s.placed);
        }
    }
    children_ = std::move(next);
}

void MovieClip::run_scripts(int frame0) {
    if (scripts_guard_ > 16) return;  // runaway gotoAndPlay loops
    ++scripts_guard_;
    // Copy: a script may jump to another frame while we iterate.
    std::vector<FrameCall> calls = (*frames_)[frame0].calls;
    for (const FrameCall& c : calls) {
        if (c.call == "stop") {
            playing_ = false;
        } else if (c.call == "play") {
            playing_ = true;
        } else if ((c.call == "gotoAndPlay" || c.call == "gotoAndStop") && !c.args.empty() && c.recv == "this") {
            bool play = c.call == "gotoAndPlay";
            if (const int* n = std::get_if<int>(&c.args[0])) goto_frame(*n - 1, play);
            else if (const std::string* s = std::get_if<std::string>(&c.args[0])) { int f = frame_of_label(*s); if (f >= 0) goto_frame(f, play); }
        } else if (c.call == "playSound" && !c.args.empty()) {
            if (const std::string* s = std::get_if<std::string>(&c.args[0])) {
                bool loop = c.args.size() > 1 && std::holds_alternative<bool>(c.args[1]) && std::get<bool>(c.args[1]);
                int n = c.args.size() > 2 && std::holds_alternative<int>(c.args[2]) ? std::get<int>(c.args[2]) : -1;
                if (host_) host_->play_sound(*s, loop, n);
            }
        } else if (c.call == "dispatchEvent" && !c.args.empty()) {
            if (const std::string* s = std::get_if<std::string>(&c.args[0])) { if (host_) host_->dispatch_event(*this, *s); }
        } else if (host_) {
            host_->unhandled_call(*this, c);
        }
    }
    --scripts_guard_;
}

void MovieClip::enter_frame(int frame0, bool sequential) {
    if (frame0 < 0 || frame0 >= static_cast<int>(frames_->size())) return;
    if (sequential) apply_ops(frame0, true);
    else rebuild_to(frame0);
    cur_ = frame0;
    run_scripts(frame0);
}

void MovieClip::goto_frame(int frame0, bool play) {
    if (frames_->empty()) return;
    frame0 = std::max(0, std::min(frame0, total_frames() - 1));
    playing_ = play;
    if (frame0 == cur_) return;  // the playhead is already there: Flash does not run the frame script again
    enter_frame(frame0, false);
}

void MovieClip::tick() {
    std::vector<MovieClip*> pre;
    for (auto& [d, c] : children_)
        if (c.clip) pre.push_back(c.clip.get());
    if (playing_ && total_frames() > 1) {
        int next = cur_ + 1;
        bool seq = true;
        if (next >= total_frames()) { next = 0; seq = false; }
        enter_frame(next, seq);
        if (stop_at_end_ && cur_ + 1 >= total_frames()) playing_ = false;
    }
    for (auto& [d, c] : children_) {
        if (c.clip && std::find(pre.begin(), pre.end(), c.clip.get()) != pre.end()) c.clip->tick();
        // a button's state sprites only run while their state is the one on screen (they are not on the display list otherwise)
        const std::vector<ButtonRecord>* recs = c.button_clips.empty() ? nullptr : lib_->button(c.char_id);
        for (size_t i = 0; i < c.button_clips.size(); ++i) {
            if (!c.button_clips[i]) continue;
            if (recs && i < recs->size() && !record_in_state((*recs)[i], c.button, *recs)) continue;
            c.button_clips[i]->tick();
        }
    }
    for (auto& o : overlays_) o.clip->tick();
}

MovieClip* MovieClip::child(const std::string& n) {
    for (auto& [d, c] : children_)
        if (c.clip && c.name == n) return c.clip.get();
    return nullptr;
}

MovieClip* MovieClip::find(const std::string& path) {
    MovieClip* cur = this;
    size_t pos = 0;
    while (cur && pos <= path.size()) {
        size_t dot = path.find('.', pos);
        std::string part = path.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
        cur = cur->child(part);
        if (dot == std::string::npos) break;
        pos = dot + 1;
    }
    return cur;
}

bool MovieClip::set_child_xy(const std::string& n, float x, float y) {
    for (auto& [d, c] : children_)
        if (c.name == n) { c.m.tx = x; c.m.ty = y; return true; }
    return false;
}

bool MovieClip::get_child_xy(const std::string& n, float* x, float* y) const {
    for (const auto& [d, c] : children_)
        if (c.name == n) { *x = c.m.tx; *y = c.m.ty; return true; }
    return false;
}

bool MovieClip::replace_child_content(const std::string& n, std::unique_ptr<MovieClip> content) {
    for (auto& [d, c] : children_)
        if (c.name == n && c.clip) {
            c.clip->content_override_ = std::move(content);
            if (c.clip->content_override_) c.clip->content_override_->parent_ = c.clip.get();
            return true;
        }
    return false;
}

bool MovieClip::child_bounds(const std::string& n, Rect* out) const {
    for (const auto& [d, c] : children_) {
        if (c.name != n) continue;
        Rect b = c.clip ? c.clip->bounds() : Rect{0, 0, 0, 0};
        if (!c.clip) {
            if (const ShapeDef* sh = lib_->shape(c.char_id)) b = sh->bounds;
            else if (const TextDef* td = lib_->text(c.char_id)) b = td->bounds;
        }
        Point p[4] = {c.m.apply({b.x0, b.y0}), c.m.apply({b.x1, b.y0}), c.m.apply({b.x1, b.y1}), c.m.apply({b.x0, b.y1})};
        Rect r{p[0].x, p[0].y, p[0].x, p[0].y};
        for (auto& q : p) { r.x0 = std::min(r.x0, q.x); r.y0 = std::min(r.y0, q.y); r.x1 = std::max(r.x1, q.x); r.y1 = std::max(r.y1, q.y); }
        *out = r;
        return true;
    }
    return false;
}

bool MovieClip::path_matrix(const std::string& path, Matrix* out) const {
    const MovieClip* cur = this;
    Matrix m;
    size_t pos = 0;
    while (cur) {
        size_t dot = path.find('.', pos);
        std::string part = path.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
        const Child* found = nullptr;
        for (const auto& [d, c] : cur->children_)
            if (c.name == part) { found = &c; break; }
        if (!found) return false;
        m = m * found->m;
        cur = found->clip.get();
        if (dot == std::string::npos) { *out = m; return true; }
        pos = dot + 1;
    }
    return false;
}

bool MovieClip::set_child_scale_x(const std::string& n, float sx) {
    for (auto& [d, c] : children_)
        if (c.name == n) { c.m.a = sx; return true; }
    return false;
}

bool MovieClip::set_child_tint(const std::string& n, int r, int g, int b) {
    for (auto& [d, c] : children_)
        if (c.name == n) {
            c.cx.mult[0] = c.cx.mult[1] = c.cx.mult[2] = 0;
            c.cx.add[0] = static_cast<float>(r); c.cx.add[1] = static_cast<float>(g); c.cx.add[2] = static_cast<float>(b);
            return true;
        }
    return false;
}

bool MovieClip::set_child_alpha(const std::string& n, float a) {
    for (auto& [d, c] : children_)
        if (c.name == n) { c.cx.mult[3] = a; return true; }
    return false;
}

bool MovieClip::set_child_visible(const std::string& n, bool v) {
    for (auto& [d, c] : children_)
        if (c.name == n) { c.hidden = !v; return true; }
    return false;
}

TextRun MovieClip::text_run_of(const Child& c, const TextDef& t) const {
    TextRun run;
    if (const FontDef* f = lib_->font(t.font)) run.font = normalize_font_name(f->name);
    else run.font = normalize_font_name(t.font_class);
    run.size = t.size;
    for (int i = 0; i < 4; ++i) run.color[i] = t.color[i];
    run.box = t.bounds;
    run.align = t.align;
    run.wrap = t.wrap || c.has_text_override;  // text set from code is laid out as a wrapped paragraph
    run.multiline = t.multiline || c.has_text_override;
    run.text = c.text;
    run.line_height = c.text_line_height;
    return run;
}

bool MovieClip::text_run(const std::string& n, TextRun* out) const {
    for (const auto& [d, c] : children_)
        if (c.name == n)
            if (const TextDef* t = lib_->text(c.char_id)) { *out = text_run_of(c, *t); return true; }
    return false;
}

void MovieClip::set_text_line_height(const std::string& n, float h) {
    for (auto& [d, c] : children_)
        if (c.name == n && lib_->text(c.char_id)) c.text_line_height = h;
}

void MovieClip::set_text(const std::string& n, const std::string& text) {
    for (auto& [d, c] : children_) {
        if (c.name == n && lib_->text(c.char_id)) { c.text = text; c.has_text_override = true; }
    }
}

void MovieClip::draw_placement(std::vector<DrawCmd>& out, const Child& c, const Matrix& w, const ColorTransform& ccx) const {
    const ColorTransform& cx_unused = ccx;
    (void)cx_unused;
    const Matrix& root_m = w;  // (kept for readability of the moved code)
    (void)root_m;
    if (c.clip) {
        c.clip->collect(out, w, ccx);
    } else if (const ShapeDef* sh = lib_->shape(c.char_id)) {
        for (const ImageQuad& q : sh->quads) {
            DrawCmd d;
            d.kind = DrawCmd::Kind::Image;
            d.image = q.image;
            d.lib = lib_;
            d.m = w * q.to_shape;
            d.cx = ccx;
            d.smooth = q.smooth;
            if (q.to_shape.axis_aligned() && q.to_shape.a != 0 && q.to_shape.d != 0) {
                float u0 = (q.clip.x0 - q.to_shape.tx) / q.to_shape.a, u1 = (q.clip.x1 - q.to_shape.tx) / q.to_shape.a;
                float v0 = (q.clip.y0 - q.to_shape.ty) / q.to_shape.d, v1 = (q.clip.y1 - q.to_shape.ty) / q.to_shape.d;
                d.src = {std::min(u0, u1), std::min(v0, v1), std::max(u0, u1), std::max(v0, v1)};
            } else {
                int iw = 0, ih = 0;
                lib_->image_size(q.image, &iw, &ih);
                d.src = {0, 0, static_cast<float>(iw), static_cast<float>(ih)};
            }
            out.push_back(d);
        }
        for (const SolidPath& sp : sh->solids) {
            DrawCmd d;
            d.kind = DrawCmd::Kind::Solid;
            d.lib = lib_;
            d.m = w;
            d.cx = ccx;
            d.solid = &sp;
            out.push_back(d);
        }
    } else if (const TextDef* t = lib_->text(c.char_id)) {
        DrawCmd d;
        d.kind = DrawCmd::Kind::Text;
        d.lib = lib_;
        d.m = w;
        d.cx = ccx;
        d.run = text_run_of(c, *t);
        out.push_back(std::move(d));
    } else if (const StaticText* st = lib_->static_text(c.char_id)) {
        for (const StaticRun& r : st->runs) {
            DrawCmd d;
            d.kind = DrawCmd::Kind::Text;
            d.lib = lib_;
            d.m = w * st->m;
            d.cx = ccx;
            if (const FontDef* f = lib_->font(r.font)) d.run.font = normalize_font_name(f->name);
            d.run.size = r.size;
            for (int i = 0; i < 4; ++i) d.run.color[i] = r.color[i];
            d.run.text = r.text;
            d.run.advances = r.advances;
            d.run.has_origin = true;
            d.run.ox = r.x;
            d.run.oy = r.y;
            out.push_back(std::move(d));
        }
    } else if (const auto* recs = lib_->button(c.char_id)) {
        // Draw the records of the current state (Up when the state has none).
        auto has_state = [&](ButtonState st) {
            for (const auto& r : *recs)
                if ((st == ButtonState::Up && r.up) || (st == ButtonState::Over && r.over) || (st == ButtonState::Down && r.down)) return true;
            return false;
        };
        ButtonState use = has_state(c.button) ? c.button : ButtonState::Up;
        std::vector<size_t> order;
        for (size_t i = 0; i < recs->size(); ++i) {
            const auto& r = (*recs)[i];
            if ((use == ButtonState::Up && r.up) || (use == ButtonState::Over && r.over) || (use == ButtonState::Down && r.down)) order.push_back(i);
        }
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return (*recs)[a].depth < (*recs)[b].depth; });
        for (size_t i : order) {
            const ButtonRecord& r = (*recs)[i];
            Child tmp;
            tmp.char_id = r.id;
            tmp.m = r.m;
            tmp.cx = r.cx;
            Matrix rw = w * r.m;
            ColorTransform rcx = ccx.concat(r.cx);
            if (i < c.button_clips.size() && c.button_clips[i]) {
                c.button_clips[i]->collect(out, rw, rcx);
            } else {
                MovieClip holder;
                holder.lib_ = lib_;
                holder.frames_ = &kNoFrames;
                Child inner;
                inner.char_id = r.id;
                holder.children_[0] = std::move(inner);
                holder.collect(out, rw, rcx);
            }
        }
    }
}

Rect MovieClip::child_bounds(const Child& c) const {
    Rect r{1e9f, 1e9f, -1e9f, -1e9f};
    auto add = [&](const Matrix& m, const Rect& b) {
        Point p[4] = {m.apply({b.x0, b.y0}), m.apply({b.x1, b.y0}), m.apply({b.x1, b.y1}), m.apply({b.x0, b.y1})};
        for (auto& q : p) { r.x0 = std::min(r.x0, q.x); r.y0 = std::min(r.y0, q.y); r.x1 = std::max(r.x1, q.x); r.y1 = std::max(r.y1, q.y); }
    };
    if (c.clip) {
        Rect cb = c.clip->bounds();
        if (!cb.empty()) add(c.m, cb);
    } else if (const ShapeDef* sh = lib_->shape(c.char_id)) {
        add(c.m, sh->bounds);
    } else if (const TextDef* t = lib_->text(c.char_id)) {
        add(c.m, t->bounds);
    } else if (const StaticText* st = lib_->static_text(c.char_id)) {
        add(c.m * st->m, st->bounds);
    } else if (const auto* recs = lib_->button(c.char_id)) {
        for (const auto& rec : *recs)
            if (const ShapeDef* sh = lib_->shape(rec.id)) add(c.m * rec.m, sh->bounds);
    }
    return r;
}

Matrix MovieClip::child_matrix(const Child& c) const {
    if (enlarge_.empty() || c.name.empty()) return c.m;
    auto it = enlarge_.find(c.name);
    if (it == enlarge_.end()) return c.m;
    Rect b = child_bounds(c);
    if (b.x0 > b.x1) return c.m;
    const float s = it->second, cx = (b.x0 + b.x1) / 2, cy = (b.y0 + b.y1) / 2;
    return Matrix{s, 0, 0, s, cx - s * cx, cy - s * cy} * c.m;
}

Rect MovieClip::bounds() const {
    Rect r{1e9f, 1e9f, -1e9f, -1e9f};
    for (const auto& [depth, c] : children_) {
        Rect b = child_bounds(c);
        if (b.x0 > b.x1) continue;
        r.x0 = std::min(r.x0, b.x0); r.y0 = std::min(r.y0, b.y0); r.x1 = std::max(r.x1, b.x1); r.y1 = std::max(r.y1, b.y1);
    }
    if (r.x0 > r.x1) return Rect{0, 0, 0, 0};
    return r;
}

void MovieClip::collect_buttons(std::vector<ButtonHit>& out, const Matrix& root_m) {
    if (!visible_) return;
    for (auto& [depth, c] : children_) {
        if (c.hidden) continue;
        Matrix w = root_m * child_matrix(c);
        if (c.clip) {
            c.clip->collect_buttons(out, w);
        } else if (const auto* recs = lib_->button(c.char_id)) {
            ButtonHit h;
            h.owner = this;
            h.depth = depth;
            h.name = c.name;
            for (const auto& r : *recs) {
                if (!r.hit) continue;
                if (const ShapeDef* sh = lib_->shape(r.id)) {
                    Matrix rw = w * r.m;
                    Rect b = sh->bounds;
                    Point p[4] = {rw.apply({b.x0, b.y0}), rw.apply({b.x1, b.y0}), rw.apply({b.x1, b.y1}), rw.apply({b.x0, b.y1})};
                    Rect box{p[0].x, p[0].y, p[0].x, p[0].y};
                    for (auto& q : p) { box.x0 = std::min(box.x0, q.x); box.y0 = std::min(box.y0, q.y); box.x1 = std::max(box.x1, q.x); box.y1 = std::max(box.y1, q.y); }
                    h.hit_rects.push_back(box);
                }
            }
            if (!h.hit_rects.empty()) out.push_back(std::move(h));
        }
    }
    for (auto& o : overlays_) o.clip->collect_buttons(out, root_m * o.m);
}

bool MovieClip::record_in_state(const ButtonRecord& r, ButtonState st, const std::vector<ButtonRecord>& all) {
    auto has = [&](ButtonState x) {
        for (const auto& q : all)
            if ((x == ButtonState::Up && q.up) || (x == ButtonState::Over && q.over) || (x == ButtonState::Down && q.down)) return true;
        return false;
    };
    ButtonState use = has(st) ? st : ButtonState::Up;  // same fallback as the drawing code
    return (use == ButtonState::Up && r.up) || (use == ButtonState::Over && r.over) || (use == ButtonState::Down && r.down);
}

void MovieClip::set_button_state(int depth, ButtonState s) {
    auto it = children_.find(depth);
    if (it == children_.end() || it->second.button == s) return;
    Child& c = it->second;
    const std::vector<ButtonRecord>* recs = c.button_clips.empty() ? nullptr : lib_->button(c.char_id);
    ButtonState prev = c.button;
    c.button = s;
    if (!recs) return;
    for (size_t i = 0; i < c.button_clips.size() && i < recs->size(); ++i)  // entering a state: its sprites start from their first frame
        if (c.button_clips[i] && record_in_state((*recs)[i], s, *recs) && !record_in_state((*recs)[i], prev, *recs)) c.button_clips[i]->goto_and_play(1);
}

void MovieClip::stop_button_over_clips_at_end(const std::string& name) {
    for (auto& [d, c] : children_) {
        if (c.name != name || c.button_clips.empty()) continue;
        const auto* recs = lib_->button(c.char_id);
        if (!recs) continue;
        for (size_t i = 0; i < c.button_clips.size() && i < recs->size(); ++i)
            if (c.button_clips[i] && ((*recs)[i].over || (*recs)[i].down)) c.button_clips[i]->stop_at_end_ = true;
    }
}

void MovieClip::set_depth_hidden(int depth, bool hidden) {
    auto it = children_.find(depth);
    if (it != children_.end()) it->second.hidden = hidden;
}

void MovieClip::stretch_depth_x(int depth, float x0, float x1) {
    if (x1 > x0) x_stretch_[depth] = {x0, x1};
    else x_stretch_.erase(depth);
}

bool MovieClip::in_named(const std::string& n) const {
    for (const MovieClip* c = this; c; c = c->parent_)
        if (c->name_ == n) return true;
    return false;
}

void MovieClip::collect(std::vector<DrawCmd>& out, const Matrix& root_m, const ColorTransform& cx) const {
    if (!visible_) return;
    bool mask_active = false;
    int mask_end = -1;
    auto draw_child = [&](const Child& c, int depth) {
        if (c.hidden) return;
        Matrix m = root_m * child_matrix(c);
        auto st = x_stretch_.find(depth);
        if (st != x_stretch_.end()) {  // widen this placement so it spans [x0, x1] (parent space) instead of its authored width
            Rect b = c.clip ? c.clip->bounds() : Rect{0, 0, 0, 0};
            if (!c.clip) {
                if (const ShapeDef* sh = lib_->shape(c.char_id)) b = sh->bounds;
            }
            float l = c.m.apply({b.x0, 0}).x, r = c.m.apply({b.x1, 0}).x;
            if (l > r) std::swap(l, r);
            if (r - l > 1e-3f) {
                float sx = (st->second.second - st->second.first) / (r - l);
                m = root_m * Matrix{sx, 0, 0, 1, st->second.first - sx * l, 0} * c.m;
            }
        }
        draw_placement(out, c, m, cx.concat(c.cx));
    };
    bool skipped_first = false;
    for (const auto& [depth, c] : children_) {
        if (content_override_ && !skipped_first) {
            skipped_first = true;
            content_override_->collect(out, root_m * c.m, cx.concat(c.cx));
            continue;
        }
        if (mask_active && depth > mask_end) {
            out.push_back(DrawCmd(DrawCmd::Kind::MaskEnd));
            mask_active = false;
        }
        if (c.clip_depth >= 0) {
            out.push_back(DrawCmd(DrawCmd::Kind::MaskBegin));
            draw_child(c, depth);
            out.push_back(DrawCmd(DrawCmd::Kind::MaskApply));
            mask_active = true;
            mask_end = c.clip_depth;
            continue;
        }
        draw_child(c, depth);
    }
    if (mask_active) out.push_back(DrawCmd(DrawCmd::Kind::MaskEnd));
    for (const auto& o : overlays_) o.clip->collect(out, root_m * o.m, cx);
}

}  // namespace sbso::engine
