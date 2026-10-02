// Same as sbso_shot but through SDL_Renderer (software driver by default), to test the SDL backend headlessly.
//   sbso_shot_sdl <extracted-dir> <swf-key> <class|root> <ticks> <out.png> [WxH] [label] [lanczos-scale]
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "engine/library.h"
#include "engine/movieclip.h"
#include "engine/sdl_renderer.h"
#include "engine/text.h"
#include "stb_image_write.h"

using namespace sbso::engine;

struct Host : ClipHost {};

int main(int argc, char** argv) {
    if (argc < 6) { std::puts("usage: sbso_shot_sdl <extracted> <swf-key> <class|root> <ticks> <out.png> [WxH] [label] [scale]"); return 2; }
    std::string root = argv[1], key = argv[2], target = argv[3];
    int ticks = std::atoi(argv[4]);
    int w = 640, h = 480;
    if (argc > 6) std::sscanf(argv[6], "%dx%d", &w, &h);
    std::string label = argc > 7 ? argv[7] : "";
    float scale = argc > 8 ? static_cast<float>(std::atof(argv[8])) : 1.0f;
    int ow = static_cast<int>(w * scale), oh = static_cast<int>(h * scale);

    if (!SDL_Init(SDL_INIT_VIDEO)) { std::printf("SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("shot", ow, oh, 0);
    SDL_Renderer* ren = win ? SDL_CreateRenderer(win, "software") : nullptr;
    if (!ren) { std::printf("renderer: %s\n", SDL_GetError()); return 1; }
    std::printf("renderer: %s\n", SDL_GetRendererName(ren));

    std::string fname = key;
    for (size_t p; (p = fname.find('/')) != std::string::npos;) fname.replace(p, 1, "__");
    Library lib;
    std::string err;
    if (!lib.load(root + "/characters/" + fname + ".json", root + "/images/" + key, &err)) { std::puts(err.c_str()); return 1; }
    Host host;
    std::unique_ptr<MovieClip> clip;
    if (target == "root") clip = MovieClip::create_root(&lib, &host);
    else { int id = lib.symbol_id(target); if (id < 0) id = std::atoi(target.c_str()); clip = MovieClip::create(&lib, id, &host); }
    if (!label.empty() && label != "-") clip->goto_label_and_play(label);
    for (int i = 0; i < ticks; ++i) clip->tick();

    std::vector<DrawCmd> cmds;
    Matrix m;
    if (target != "root") { m.tx = w / 2.0f; m.ty = h / 2.0f; }
    clip->collect(cmds, m, ColorTransform{});

    FontBank bank;
    bank.add_directory(root + "/fonts");
    bank.set_fallbacks({"unibody8black"});
    TextRaster tr(&bank);
    SdlRenderer r(ren, root + "/images");
    r.set_text(&tr);
    r.set_asset_filter(scale, scale > 1.01f ? sbso::Filter::Lanczos3 : sbso::Filter::Nearest);
    SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
    SDL_RenderClear(ren);
    Matrix view{scale, 0, 0, scale, 0, 0};
    r.draw(cmds, view, ow, oh);
    int pw, ph;
    std::vector<unsigned char> px;
    if (!r.read_pixels(&pw, &ph, &px)) { std::printf("read: %s\n", SDL_GetError()); return 1; }
    std::printf("frame %d, %zu cmds, %dx%d\n", clip->current_frame() + 1, cmds.size(), pw, ph);
    stbi_write_png(argv[5], pw, ph, 4, px.data(), pw * 4);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
