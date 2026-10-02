// pugixml loading through the virtual file system.
#pragma once
#include <string>

#include "core/vfs.h"
#include "pugixml.hpp"

namespace sbso::vfs {

inline pugi::xml_parse_result load_xml(pugi::xml_document& doc, const std::string& path) {
    std::string text;
    if (!read_text(path, &text)) {
        pugi::xml_parse_result r;
        r.status = pugi::status_file_not_found;
        return r;
    }
    return doc.load_buffer(text.data(), text.size());
}

}  // namespace sbso::vfs
