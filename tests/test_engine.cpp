// Needs the extractor output (extracted/); skipped when it is not present (assets are not in the repo).
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "engine/library.h"
#include "engine/movieclip.h"
#include "engine/soft_renderer.h"

using namespace sbso::engine;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

struct Host : ClipHost {
    int sounds = 0, events = 0, unhandled = 0;
    void play_sound(const std::string&, bool, int) override { ++sounds; }
    void dispatch_event(MovieClip&, const std::string&) override { ++events; }
    void unhandled_call(MovieClip&, const FrameCall&) override { ++unhandled; }
};

static std::string key_of(const fs::path& json, const fs::path& chars_dir) {
    std::string k = json.stem().string();
    for (size_t p; (p = k.find("__")) != std::string::npos;) k.replace(p, 2, "/");
    (void)chars_dir;
    return k;
}

static void test_titlescreen(const std::string& root) {
    Library lib;
    std::string err;
    CHECK(lib.load(root + "/characters/titlescreen.json", root + "/images/titlescreen", &err));
    CHECK(lib.symbol_id("titlescreen") == 67);
    Host host;
    auto clip = MovieClip::create(&lib, 67, &host);
    CHECK(clip->total_frames() == 233);
    for (int i = 0; i < 100; ++i) clip->tick();
    // TitleScreen.preloadAnim waits for currentFrame == 26 and the frame script stops the clip there.
    CHECK(clip->current_frame() == 25 && !clip->playing());
    CHECK(clip->goto_label_and_play("menu"));
    for (int i = 0; i < 148; ++i) clip->tick();
    CHECK(clip->current_frame() == 174);  // the Big Fish Games splash is on screen here
    std::vector<DrawCmd> cmds;
    clip->collect(cmds, Matrix{1, 0, 0, 1, 320, 240}, ColorTransform{});
    CHECK(!cmds.empty());
    SoftRenderer r(640, 480);
    r.set_images_root(root + "/images");
    r.clear(0, 0, 0, 1);
    r.draw(cmds);
    auto px = r.rgba8();
    int lit = 0;
    for (size_t i = 0; i < px.size(); i += 4) if (px[i] + px[i + 1] + px[i + 2] > 60) ++lit;
    CHECK(lit > 20000);  // the logo covers a large part of the frame
}

static void test_smoke(const std::string& root) {
    int libs = 0, clips = 0;
    for (const auto& e : fs::directory_iterator(root + "/characters")) {
        if (e.path().extension() != ".json") continue;
        Library lib;
        std::string err;
        std::string key = key_of(e.path(), root + "/characters");
        if (!lib.load(e.path().string(), root + "/images/" + key, &err)) { std::printf("%s\n", err.c_str()); ++failures; continue; }
        ++libs;
        Host host;
        SoftRenderer r(64, 48);
        r.set_images_root(root + "/images");
        // Instantiate every named class, tick a few frames and collect draw commands.
        for (int id = 1; id < 100000; ++id) {
            const std::string& n = lib.symbol_name(id);
            if (n.empty() || !lib.sprite(id)) continue;
            auto c = MovieClip::create(&lib, id, &host);
            for (int t = 0; t < 4; ++t) c->tick();
            std::vector<DrawCmd> cmds;
            c->collect(cmds, Matrix{}, ColorTransform{});
            ++clips;
        }
    }
    std::printf("smoke: %d libraries, %d clips instantiated\n", libs, clips);
    CHECK(libs >= 50 && clips > 300);
}

int main() {
    const char* env = std::getenv("SBSO_EXTRACTED");
    std::string root = env ? env : EXTRACTED_DIR;
    if (!fs::exists(root + "/characters/titlescreen.json")) {
        std::printf("skipped: no extractor output at %s\n", root.c_str());
        return 0;
    }
    test_titlescreen(root);
    test_smoke(root);
    std::printf(failures ? "%d FAILED\n" : "all engine tests passed\n", failures);
    return failures ? 1 : 0;
}
