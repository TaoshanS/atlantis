#include "cutscene.h"
#include "core/vfs_xml.h"

#include <cstdlib>

#include "pugixml.hpp"

namespace sbso::game {

bool load_cutscene(const std::string& maps_dir, const std::string& file, std::vector<CutStep>* out, std::string* err) {
    pugi::xml_document doc;
    auto res = vfs::load_xml(doc, (maps_dir + "/" + file));
    if (!res) { if (err) *err = file + ": " + res.description(); return false; }
    int speaker_index = 0;  // document-order index over every <speaker>, as in the localisation table
    auto parse = [&](pugi::xml_node node) {
        Dialogue d;
        for (auto sp : node.children("speaker")) {
            DialogueLine l;
            l.frame = sp.attribute("frame").value();
            l.text = sp.attribute("text").value();
            l.key = "xml:" + file + ":speaker.text[" + std::to_string(speaker_index++) + "]";
            d.push_back(std::move(l));
        }
        return d;
    };
    out->clear();
    auto info = doc.child("info");
    for (auto n : info.children()) {
        CutStep s;
        s.type = n.name();
        if (s.type == "choice") {
            for (auto c : n.children()) {
                std::string cn = c.name();
                if (cn == "dialogue") s.dialogue = parse(c);
                else if (cn == "shortcut") s.shortcut = parse(c);
            }
        } else if (s.type == "dialogue") {
            s.dialogue = parse(n);
        } else if (s.type == "move") {
            s.actor = n.attribute("actor").value();
            s.from_x = n.attribute("from_x").as_int();
            s.from_y = n.attribute("from_y").as_int();
            s.to_x = n.attribute("to_x") ? n.attribute("to_x").as_int() : s.from_x;
            s.to_y = n.attribute("to_y") ? n.attribute("to_y").as_int() : s.from_y;
        } else if (s.type == "remove") {
            s.from_x = n.attribute("from_x").as_int();
            s.from_y = n.attribute("from_y").as_int();
        }
        out->push_back(std::move(s));
    }
    return true;
}

}  // namespace sbso::game
