#include "vfs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>

namespace sbso::vfs {

namespace {

struct Entry { std::uint64_t offset = 0, size = 0; };
struct Pack {
    std::string file, root;
    std::map<std::string, Entry> index;
};
std::vector<std::unique_ptr<Pack>>& packs() { static std::vector<std::unique_ptr<Pack>> p; return p; }

std::function<bool(const std::string&, std::vector<unsigned char>*)>& reader() {
    static std::function<bool(const std::string&, std::vector<unsigned char>*)> r;
    return r;
}

bool read_file_range(const std::string& path, std::uint64_t off, std::uint64_t size, bool whole, std::vector<unsigned char>* out) {
    if (reader() && whole) return reader()(path, out);
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    if (whole) {
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n < 0) { std::fclose(f); return false; }
        size = static_cast<std::uint64_t>(n);
    } else if (std::fseek(f, static_cast<long>(off), SEEK_SET) != 0) { std::fclose(f); return false; }
    out->resize(static_cast<size_t>(size));
    size_t got = size ? std::fread(out->data(), 1, static_cast<size_t>(size), f) : 0;
    std::fclose(f);
    return got == size;
}

std::uint64_t rd(const unsigned char* p, int n) {
    std::uint64_t v = 0;
    for (int i = n - 1; i >= 0; --i) v = v << 8 | p[i];
    return v;
}

const Entry* find_entry(const std::string& path, const Pack** pack) {
    for (auto& p : packs()) {
        if (path.size() > p->root.size() + 1 && path.compare(0, p->root.size(), p->root) == 0 && path[p->root.size()] == '/') {
            auto it = p->index.find(path.substr(p->root.size() + 1));
            if (it != p->index.end()) { *pack = p.get(); return &it->second; }
        }
    }
    return nullptr;
}

}  // namespace

void set_file_reader(std::function<bool(const std::string&, std::vector<unsigned char>*)> r) { reader() = std::move(r); }

bool is_pack(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char m[8] = {};
    size_t n = std::fread(m, 1, 8, f);
    std::fclose(f);
    return n == 8 && !std::memcmp(m, "SBSOPAK1", 8);
}

// Layout: "SBSOPAK1", u32 count, u64 index_offset, u64 index_size; index: count x {u16 name_len, name, u64 offset, u64 size}.
bool mount_pack(const std::string& file, const std::string& root) {
    std::vector<unsigned char> head;
    if (!read_file_range(file, 0, 28, false, &head) || head.size() < 28 || std::memcmp(head.data(), "SBSOPAK1", 8)) return false;
    std::uint32_t count = static_cast<std::uint32_t>(rd(&head[8], 4));
    std::uint64_t index_off = rd(&head[12], 8);
    std::uint64_t index_size = rd(&head[20], 8);
    std::vector<unsigned char> all;
    if (!read_file_range(file, index_off, index_size, false, &all)) return false;
    auto pk = std::make_unique<Pack>();
    pk->file = file;
    pk->root = root;
    size_t pos = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (pos + 2 > all.size()) return false;
        size_t nl = rd(&all[pos], 2);
        pos += 2;
        if (pos + nl + 16 > all.size()) return false;
        std::string name(reinterpret_cast<const char*>(&all[pos]), nl);
        pos += nl;
        Entry e;
        e.offset = rd(&all[pos], 8);
        e.size = rd(&all[pos + 8], 8);
        pos += 16;
        pk->index[name] = e;
    }
    packs().push_back(std::move(pk));
    return true;
}

bool read(const std::string& path, std::vector<unsigned char>* out) {
    const Pack* pk = nullptr;
    if (const Entry* e = find_entry(path, &pk)) return read_file_range(pk->file, e->offset, e->size, false, out);
    return read_file_range(path, 0, 0, true, out);
}

bool read_prefix(const std::string& path, size_t n, std::vector<unsigned char>* out) {
    const Pack* pk = nullptr;
    if (const Entry* e = find_entry(path, &pk)) return read_file_range(pk->file, e->offset, std::min<std::uint64_t>(e->size, n), false, out);
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out->resize(n);
    out->resize(std::fread(out->data(), 1, n, f));
    std::fclose(f);
    return true;
}

bool read_text(const std::string& path, std::string* out) {
    std::vector<unsigned char> d;
    if (!read(path, &d)) return false;
    out->assign(d.begin(), d.end());
    return true;
}

bool exists(const std::string& path) {
    const Pack* pk = nullptr;
    if (find_entry(path, &pk)) return true;
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::vector<std::string> list(const std::string& dir) {
    std::vector<std::string> v;
    for (auto& p : packs()) {
        std::string prefix = dir.size() > p->root.size() && dir.compare(0, p->root.size(), p->root) == 0 && dir[p->root.size()] == '/'
                                 ? dir.substr(p->root.size() + 1) + "/" : std::string();
        if (dir == p->root) prefix.clear(); else if (prefix.empty()) continue;
        for (auto it = p->index.lower_bound(prefix); it != p->index.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it) {
            std::string rest = it->first.substr(prefix.size());
            if (rest.find('/') == std::string::npos) v.push_back(rest);
        }
    }
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_regular_file(ec)) v.push_back(e.path().filename().string());
    std::sort(v.begin(), v.end());
    return v;
}

}  // namespace sbso::vfs
