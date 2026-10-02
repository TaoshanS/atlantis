// Headless screenshot tool: runs a clip from an extracted SWF for N ticks and renders it.
//   sbso_shot <extracted-dir> <swf-key> <sprite-class|root> <ticks> <out.png> [WxH]
#include <cstdio>
#include <cstdlib>
#include <string>

#include "engine/library.h"
#include "engine/movieclip.h"
#include "engine/soft_renderer.h"
#include "engine/text.h"

using namespace sbso::engine;

struct Host : ClipHost {
    void play_sound(const std::string& n, bool, int) override { std::printf("  sound: %s\n", n.c_str()); }
    void dispatch_event(MovieClip&, const std::string& e) override { std::printf("  event: %s\n", e.c_str()); }
};

int main(int argc, char** argv) {
    if (argc < 6) { std::puts("usage: sbso_shot <extracted> <swf-key> <class|root> <ticks> <out.png> [WxH] [label]"); return 2; }
    std::string root = argv[1], key = argv[2], target = argv[3];
    int ticks = std::atoi(argv[4]);
    int w = 640, h = 480;
    if (argc > 6) std::sscanf(argv[6], "%dx%d", &w, &h);
    std::string fname = key;
    for (size_t p; (p = fname.find('/')) != std::string::npos;) fname.replace(p, 1, "__");
    Library lib;
    std::string err;
    if (!lib.load(root + "/characters/" + fname + ".json", root + "/images/" + key, &err)) { std::puts(err.c_str()); return 1; }
    Host host;
    std::unique_ptr<MovieClip> clip;
    if (target == "root") clip = MovieClip::create_root(&lib, &host);
    else {
        int id = lib.symbol_id(target);
        if (id < 0) id = std::atoi(target.c_str());
        clip = MovieClip::create(&lib, id, &host);
    }
    std::string label = argc > 7 ? argv[7] : "";  // optional: gotoAndPlay(label) before ticking
    if (!label.empty() && !clip->goto_label_and_play(label)) std::printf("no label %s\n", label.c_str());
    for (int i = 0; i < ticks; ++i) clip->tick();
    std::printf("frame %d of %d playing=%d\n", clip->current_frame() + 1, clip->total_frames(), clip->playing());
    std::vector<DrawCmd> cmds;
    Matrix m;
    if (target != "root") { m.tx = w / 2.0f; m.ty = h / 2.0f; }
    clip->collect(cmds, m, ColorTransform{});
    FontBank bank;
    bank.add_directory(root + "/fonts");
    bank.set_fallbacks({"unibody8black"});
    TextRaster tr(&bank);
    SoftRenderer r(w, h);
    r.set_text(&tr);
    r.set_images_root(root + "/images");
    r.clear(0.23f, 0.23f, 0.23f, 1);
    r.draw(cmds);
    std::printf("%zu draw commands, solids skipped=%d\n", cmds.size(), lib.solids_skipped());
    return r.save_png(argv[5]) ? 0 : 1;
}
