#include "library.h"
#include "image_io.h"
#include "core/vfs.h"

#include <fstream>
#include <sstream>

#include "json.hpp"
#include "stb_image.h"

namespace sbso::engine {
namespace {

using nlohmann::json;

Matrix matrix_from(const json& m) {
    return {m[0].get<float>(), m[1].get<float>(), m[2].get<float>(), m[3].get<float>(), m[4].get<float>(), m[5].get<float>()};
}

ColorTransform cx_from(const json& c) {
    ColorTransform t;
    for (int i = 0; i < 4; ++i) {
        t.mult[i] = c[i].get<float>();
        t.add[i] = c[4 + i].get<float>();
    }
    return t;
}

Value value_from(const json& v) {
    // new Event("NAME") is recorded as {"new": "Event", "args": ["NAME"]}: keep the event name.
    if (v.is_object() && v.contains("new") && v.contains("args") && v["args"].is_array() && !v["args"].empty() && v["args"][0].is_string())
        return v["args"][0].get<std::string>();
    if (v.is_number_integer()) return v.get<int>();
    if (v.is_number()) return v.get<double>();
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_string()) return v.get<std::string>();
    return std::monostate{};
}

void flatten(const json& segs, std::vector<Point>& out) {
    Point cur{};
    for (const auto& s : segs) {
        const std::string k = s[0].get<std::string>();
        if (k == "M") {
            cur = {s[1].get<float>(), s[2].get<float>()};
            out.push_back(cur);
        } else if (k == "L") {
            cur = {s[1].get<float>(), s[2].get<float>()};
            out.push_back(cur);
        } else {
            Point c{s[1].get<float>(), s[2].get<float>()}, e{s[3].get<float>(), s[4].get<float>()};
            for (int i = 1; i <= 8; ++i) {
                float t = i / 8.0f, u = 1 - t;
                out.push_back({u * u * cur.x + 2 * u * t * c.x + t * t * e.x, u * u * cur.y + 2 * u * t * c.y + t * t * e.y});
            }
            cur = e;
        }
    }
}

Rect bbox_of(const std::vector<Point>& pts) {
    Rect r{1e9f, 1e9f, -1e9f, -1e9f};
    for (const Point& p : pts) {
        r.x0 = std::min(r.x0, p.x); r.y0 = std::min(r.y0, p.y);
        r.x1 = std::max(r.x1, p.x); r.y1 = std::max(r.y1, p.y);
    }
    return r;
}

std::vector<Frame> frames_from(const json& frames, const json* scripts) {
    std::vector<Frame> out;
    for (const auto& f : frames) {
        Frame fr;
        if (f.contains("label")) fr.label = f["label"].get<std::string>();
        for (const auto& o : f["ops"]) {
            Op op;
            if (o.contains("remove")) {
                op.remove = true;
                op.depth = o["remove"].get<int>();
            } else if (o.contains("sound")) {
                op.sound = o["sound"].get<int>();
                op.depth = -1;
            } else {
                op.depth = o["depth"].get<int>();
                if (o.contains("id")) op.id = o["id"].get<int>();
                op.move = o.value("move", false);
                if (o.contains("m")) { op.has_matrix = true; op.m = matrix_from(o["m"]); }
                if (o.contains("cx")) { op.has_cx = true; op.cx = cx_from(o["cx"]); }
                if (o.contains("clip")) op.clip_depth = o["clip"].get<int>();
                if (o.contains("name")) op.name = o["name"].get<std::string>();
            }
            fr.ops.push_back(std::move(op));
        }
        out.push_back(std::move(fr));
    }
    if (scripts && scripts->is_object()) {
        for (auto it = scripts->begin(); it != scripts->end(); ++it) {
            size_t idx = static_cast<size_t>(std::stoul(it.key()));
            if (idx >= out.size()) continue;
            for (const auto& c : it.value()) {
                FrameCall fc;
                fc.call = c["call"].get<std::string>();
                if (c["recv"].is_string()) fc.recv = c["recv"].get<std::string>();
                for (const auto& a : c["args"]) fc.args.push_back(value_from(a));
                out[idx].calls.push_back(std::move(fc));
            }
        }
    }
    return out;
}

}  // namespace

const std::string& Library::symbol_name(int id) const {
    static const std::string empty;
    auto it = names_.find(id);
    return it == names_.end() ? empty : it->second;
}

std::string Library::image_file(const std::string& images_root, int id) const {
    // name_ is .../characters/<key with '/' written as '__'>.json
    size_t slash = name_.find_last_of("/\\");
    std::string stem = name_.substr(slash == std::string::npos ? 0 : slash + 1);
    if (stem.size() > 5 && stem.compare(stem.size() - 5, 5, ".json") == 0) stem.resize(stem.size() - 5);
    for (size_t p; (p = stem.find("__")) != std::string::npos;) stem.replace(p, 2, "/");
    return image_path(images_root + "/" + stem + "/" + std::to_string(id));
}

bool Library::image_size(int id, int* w, int* h) const {
    auto it = image_sizes_.find(id);
    if (it == image_sizes_.end()) {
        int iw = 0, ih = 0;
        if (!image_dims(image_path(images_dir_ + "/" + std::to_string(id)), &iw, &ih)) iw = ih = 0;
        it = image_sizes_.emplace(id, std::make_pair(iw, ih)).first;
    }
    if (it->second.first <= 0) return false;
    *w = it->second.first;
    *h = it->second.second;
    return true;
}

bool Library::load(const std::string& path, const std::string& images_dir, std::string* err) {
    std::string text;
    if (!vfs::read_text(path, &text)) { if (err) *err = "cannot open " + path; return false; }
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded()) { if (err) *err = "invalid json: " + path; return false; }
    name_ = path;
    images_dir_ = images_dir;
    if (j.contains("header")) {
        auto r = j["header"]["rect"];
        stage_ = {r[0].get<float>(), r[2].get<float>(), r[1].get<float>(), r[3].get<float>()};
        frame_rate_ = j["header"]["frame_rate"].get<float>();
    }
    for (auto it = j["symbols"].begin(); it != j["symbols"].end(); ++it) {
        int id = std::stoi(it.key());
        names_[id] = it.value().get<std::string>();
        by_name_[it.value().get<std::string>()] = id;
    }
    auto size_of = [&](int id) -> std::pair<int, int> {
        auto it = image_sizes_.find(id);
        if (it != image_sizes_.end()) return it->second;
        int w = 0, h = 0;
        if (!image_dims(image_path(images_dir + "/" + std::to_string(id)), &w, &h)) w = h = 0;
        image_sizes_[id] = {w, h};
        return {w, h};
    };
    for (auto it = j["shapes"].begin(); it != j["shapes"].end(); ++it) {
        const json& s = it.value();
        ShapeDef sd;
        auto b = s["bounds"];
        sd.bounds = {b[0].get<float>(), b[2].get<float>(), b[1].get<float>(), b[3].get<float>()};
        const json& fills = s["fills"];
        for (const auto& p : s["paths"]) {
            int fi = p[1].get<int>() ? p[1].get<int>() : p[0].get<int>();
            if (fi <= 0 || fi > static_cast<int>(fills.size())) continue;
            const json& fill = fills[fi - 1];
            std::vector<Point> pts;
            flatten(p[3], pts);
            if (pts.empty()) continue;
            std::string t = fill["t"].get<std::string>();
            if (t == "bitmap") {
                int id = fill["id"].get<int>();
                if (id == 0xFFFF) continue;
                auto [iw, ih] = size_of(id);
                if (iw <= 0) continue;
                ImageQuad q;
                q.image = id;
                json m = fill["m"];
                q.to_shape = {m[0].get<float>() / 20, m[1].get<float>() / 20, m[2].get<float>() / 20, m[3].get<float>() / 20, m[4].get<float>(), m[5].get<float>()};
                q.smooth = fill.value("smooth", true);
                Rect clip = bbox_of(pts);
                if (q.to_shape.axis_aligned()) {
                    Rect ext{q.to_shape.tx, q.to_shape.ty, q.to_shape.tx + q.to_shape.a * iw, q.to_shape.ty + q.to_shape.d * ih};
                    if (ext.x1 < ext.x0) std::swap(ext.x0, ext.x1);
                    if (ext.y1 < ext.y0) std::swap(ext.y0, ext.y1);
                    clip = clip.intersect(ext);
                }
                q.clip = clip;
                bool dup = false;
                for (const auto& e : sd.quads)
                    if (e.image == q.image && e.clip.x0 == q.clip.x0 && e.clip.y0 == q.clip.y0 && e.clip.x1 == q.clip.x1 && e.clip.y1 == q.clip.y1) dup = true;
                if (!dup && !q.clip.empty()) sd.quads.push_back(q);
            } else if (t == "solid") {
                SolidPath sp;
                for (int i = 0; i < 4; ++i) sp.rgba[i] = fill["c"][i].get<float>() / 255.0f;
                sp.contours.push_back(std::move(pts));
                sd.solids.push_back(std::move(sp));
                ++solids_;
            }
        }
        shapes_[std::stoi(it.key())] = std::move(sd);
    }
    for (auto it = j["sprites"].begin(); it != j["sprites"].end(); ++it) {
        const json* sc = it.value().contains("scripts") ? &it.value()["scripts"] : nullptr;
        sprites_[std::stoi(it.key())] = frames_from(it.value()["frames"], sc);
    }
    const json* rs = j.contains("root_scripts") ? &j["root_scripts"] : nullptr;
    root_ = frames_from(j["root"], rs);
    for (auto it = j["buttons"].begin(); it != j["buttons"].end(); ++it) {
        std::vector<ButtonRecord> recs;
        for (const auto& r : it.value()["records"]) {
            ButtonRecord br;
            br.id = r["id"].get<int>();
            br.depth = r["depth"].get<int>();
            br.m = matrix_from(r["m"]);
            br.cx = cx_from(r["cx"]);
            for (const auto& s : r["states"]) {
                std::string st = s.get<std::string>();
                if (st == "up") br.up = true;
                else if (st == "over") br.over = true;
                else if (st == "down") br.down = true;
                else if (st == "hit") br.hit = true;
            }
            recs.push_back(br);
        }
        buttons_[std::stoi(it.key())] = std::move(recs);
    }
    for (auto it = j["texts"].begin(); it != j["texts"].end(); ++it) {
        const json& t = it.value();
        TextDef td;
        auto b = t["bounds"];
        td.bounds = {b[0].get<float>(), b[2].get<float>(), b[1].get<float>(), b[3].get<float>()};
        td.text = t.value("text", std::string());
        td.variable = t.value("var", std::string());
        td.font_class = t.value("fontclass", std::string());
        td.size = t.value("size", 12.0f);
        td.font = t.value("font", -1);
        td.align = t.value("align", 0);
        td.html = t.value("html", false);
        td.multiline = t.value("multiline", false);
        td.wrap = t.value("wrap", false);
        td.autosize = t.value("autosize", false);
        td.readonly = t.value("readonly", true);
        if (t.contains("color")) for (int i = 0; i < 4; ++i) td.color[i] = t["color"][i].get<float>() / 255.0f;
        texts_[std::stoi(it.key())] = td;
    }
    for (auto it = j["fonts"].begin(); it != j["fonts"].end(); ++it) {
        FontDef fd;
        fd.name = it.value().value("name", std::string());
        fd.glyphs = it.value().value("glyphs", 0);
        if (it.value().contains("codes")) fd.codes = it.value()["codes"].get<std::vector<int>>();
        fonts_[std::stoi(it.key())] = fd;
    }
    if (j.contains("statics")) {
        for (auto it = j["statics"].begin(); it != j["statics"].end(); ++it) {
            StaticText st;
            auto b = it.value()["bounds"];
            st.bounds = {b[0].get<float>(), b[2].get<float>(), b[1].get<float>(), b[3].get<float>()};
            st.m = matrix_from(it.value()["m"]);
            for (const auto& r : it.value()["runs"]) {
                StaticRun run;
                run.font = r["font"].is_null() ? -1 : r["font"].get<int>();
                run.size = r["size"].get<float>();
                for (int i = 0; i < 4; ++i) run.color[i] = r["color"][i].get<float>() / 255.0f;
                run.x = r["x"].get<float>();
                run.y = r["y"].get<float>();
                const FontDef* fd = nullptr;
                auto fit = fonts_.find(run.font);
                if (fit != fonts_.end()) fd = &fit->second;
                for (size_t gi = 0; gi < r["glyphs"].size(); ++gi) {
                    int g = r["glyphs"][gi].get<int>();
                    int code = (fd && g < static_cast<int>(fd->codes.size())) ? fd->codes[g] : '?';
                    // UTF-8 encode (BMP only, as in DefineFont3)
                    if (code < 0x80) run.text += static_cast<char>(code);
                    else if (code < 0x800) { run.text += static_cast<char>(0xC0 | (code >> 6)); run.text += static_cast<char>(0x80 | (code & 63)); }
                    else { run.text += static_cast<char>(0xE0 | (code >> 12)); run.text += static_cast<char>(0x80 | ((code >> 6) & 63)); run.text += static_cast<char>(0x80 | (code & 63)); }
                    run.advances.push_back(r["advances"][gi].get<float>());
                }
                st.runs.push_back(std::move(run));
            }
            statics_[std::stoi(it.key())] = std::move(st);
        }
    }
    return true;
}

}  // namespace sbso::engine
