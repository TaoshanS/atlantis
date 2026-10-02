// Decoding of the extracted bitmaps: lossless WebP (current extractions) or PNG (older ones), read through the VFS.
#pragma once
#include <string>
#include <vector>

namespace sbso::engine {

// The bitmap of `stem` (path without extension): stem.webp if present, else stem.png.
std::string image_path(const std::string& stem);
// Width / height from the file header (cheap: reads only its first bytes).
bool image_dims(const std::string& path, int* w, int* h);
// Straight (non-premultiplied) RGBA8.
bool decode_image(const std::string& path, int* w, int* h, std::vector<unsigned char>* rgba);

}  // namespace sbso::engine
