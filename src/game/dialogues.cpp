#include "dialogues.h"
#include "core/vfs_xml.h"

#include "pugixml.hpp"

namespace sbso::game {
namespace {

Dialogue read(pugi::xml_node parent, std::map<const void*, int>& index) {
    Dialogue d;
    for (auto s : parent.children("speaker")) {
        DialogueLine l;
        l.frame = s.attribute("frame").value();
        l.text = s.attribute("text").value();
        l.key = "xml:SBSO2_CONFIG.xml:speaker.text[" + std::to_string(index[s.internal_object()]) + "]";
        d.push_back(std::move(l));
    }
    return d;
}

}  // namespace

bool Dialogues::load(const std::string& maps_dir, std::string* err) {
    pugi::xml_document doc;
    auto res = vfs::load_xml(doc, (maps_dir + "/SBSO2_CONFIG.xml"));
    if (!res) { if (err) *err = std::string("SBSO2_CONFIG.xml: ") + res.description(); return false; }
    // number every <speaker> in document order
    std::map<const void*, int> index;
    int n = 0;
    for (auto node : doc.select_nodes("//speaker")) index[node.node().internal_object()] = n++;
    auto root = doc.document_element();
    krusty_krab = read(root.child("intro").child("krab_interior"), index);
    int stage = 0;
    for (auto s : root.child("stages").children("stage")) {
        ++stage;
        for (auto id : s.child("introList").children("introDialogue"))
            stage_intro[{stage, id.attribute("level").as_int()}] = read(id, index);
        boss[stage] = read(s.child("bossDialogue"), index);
        boss_end[stage] = read(s.child("bossEndDialogue"), index);
    }
    for (auto b : root.child("beltData").children("belt")) belt.push_back(read(b.child("dialogue"), index));
    for (auto c : root.child("cardDialogues").children("card")) card[c.attribute("name").value()] = read(c.child("dialogue"), index);
    for (auto m : root.child("minigames").children("minigame")) minigame.push_back(read(m.child("dialogue"), index));
    return true;
}

const Dialogue* Dialogues::intro_for(int stage, int level) const {
    auto it = stage_intro.find({stage, level});
    return it == stage_intro.end() || it->second.empty() ? nullptr : &it->second;
}

}  // namespace sbso::game
