// Builds a small pack with tools/pack/make_pack.py and reads it back through the VFS.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/vfs.h"

namespace fs = std::filesystem;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main() {
    fs::path tmp = fs::path(TEST_TMP_DIR) / "vfs_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "ext" / "sub");
    fs::create_directories(tmp / "maps");
    std::ofstream(tmp / "ext" / "a.txt") << "hello";
    std::ofstream(tmp / "ext" / "sub" / "b.bin", std::ios::binary) << std::string("\0\1\2", 3);
    std::ofstream(tmp / "maps" / "m.xml") << "<x/>";
    std::string cmd = std::string("python3 \"") + SOURCE_DIR + "/tools/pack/make_pack.py\" \"" + (tmp / "ext").string() + "\" \"" + (tmp / "maps").string() + "\" \"" + (tmp / "t.pak").string() + "\" > /dev/null";
    CHECK(std::system(cmd.c_str()) == 0);
    std::string pak = (tmp / "t.pak").string();
    CHECK(sbso::vfs::is_pack(pak));
    CHECK(!sbso::vfs::is_pack((tmp / "ext" / "a.txt").string()));
    CHECK(sbso::vfs::mount_pack(pak, "pak:"));
    std::string t;
    CHECK(sbso::vfs::read_text("pak:/a.txt", &t) && t == "hello");
    std::vector<unsigned char> b;
    CHECK(sbso::vfs::read("pak:/sub/b.bin", &b) && b.size() == 3 && b[2] == 2);
    CHECK(sbso::vfs::read_text("pak:/maps/m.xml", &t) && t == "<x/>");
    CHECK(sbso::vfs::exists("pak:/sub/b.bin") && !sbso::vfs::exists("pak:/nope"));
    auto l = sbso::vfs::list("pak:");
    CHECK(l.size() == 1 && l[0] == "a.txt");
    CHECK(sbso::vfs::list("pak:/sub").size() == 1);
    CHECK(sbso::vfs::read_text((tmp / "ext" / "a.txt").string(), &t) && t == "hello");  // plain disk still works
    std::puts(fails ? "FAILED" : "vfs ok");
    return fails ? 1 : 0;
}
