// Virtual file system: asset reads go through here so the data can live in a single pack file (mobile) or a plain directory (desktop).
#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace sbso::vfs {

// Mounts a pack made by tools/pack/make_pack.py; its files appear under "<root>/<name>". `root` is a virtual prefix such as "pak:".
bool mount_pack(const std::string& pack_file, const std::string& root);
// True when `path` is a regular file that starts with the pack magic (the --extracted argument may be either a directory or a pack).
bool is_pack(const std::string& path);

bool read(const std::string& path, std::vector<unsigned char>* out);
// First `n` bytes (fewer if the file is shorter): enough for image headers without loading the whole file.
bool read_prefix(const std::string& path, size_t n, std::vector<unsigned char>* out);
bool read_text(const std::string& path, std::string* out);
bool exists(const std::string& path);
// File names (not paths) directly inside `dir`, sorted.
std::vector<std::string> list(const std::string& dir);

// Platform hook for reading real files (Android APK assets, iOS bundle); the default uses the C library.
void set_file_reader(std::function<bool(const std::string&, std::vector<unsigned char>*)> reader);

}  // namespace sbso::vfs
