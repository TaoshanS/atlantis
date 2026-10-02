#include "config.h"
#include "core/vfs_xml.h"

#include <algorithm>
#include <cstdlib>

#include "pugixml.hpp"

namespace sbso::game {

bool Config::load(const std::string& maps_dir, std::string* err) {
    pugi::xml_document doc;
    auto res = vfs::load_xml(doc, (maps_dir + "/SBSO2_CONFIG.xml"));
    if (!res) { if (err) *err = std::string("SBSO2_CONFIG.xml: ") + res.description(); return false; }
    auto root = doc.document_element();
    for (auto s : root.child("stages").children("stage")) {
        StageInfo st;
        st.name = s.attribute("name").value();
        st.num_levels = s.attribute("numLevels").as_int();
        st.barrel_prob = s.attribute("barrels").as_int();
        stages.push_back(st);
    }
    for (auto b : root.child("beltData").children("belt")) {
        BeltInfo bi;
        bi.name = b.attribute("name").value();
        bi.capacity = b.attribute("capacity").as_int();
        bi.plus_health = b.attribute("plusHealth").as_int();
        bi.plus_energy = b.attribute("plusEnergy").as_int();
        belts.push_back(bi);
    }
    auto split = [](const std::string& text) {
        std::vector<std::string> out;
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t c = text.find(',', pos);
            std::string t = text.substr(pos, c == std::string::npos ? std::string::npos : c - pos);
            if (!t.empty()) out.push_back(t);
            if (c == std::string::npos) break;
            pos = c + 1;
        }
        return out;
    };
    for (auto m : root.child("minigames").children("minigame")) {
        MinigameInfo mi;
        mi.whammies = m.attribute("whammy").as_int();
        mi.num_cards = m.attribute("total").as_int();
        mi.cost = m.attribute("cost").as_int();
        mi.map = m.attribute("map").value();
        mi.anim = m.attribute("anim").value();
        mi.rares = split(m.child("rareCards").text().as_string());
        mi.commons = split(m.child("commonCards").text().as_string());
        minigames.push_back(std::move(mi));
    }
    std::vector<std::string> added;
    for (auto c : root.child("DefaultData").child("defaultProfileCards").children("card")) {
        DefaultCardInfo d;
        d.type = c.attribute("type").value();
        d.name = c.attribute("name").value();
        d.in_belt = std::string(c.attribute("inBelt").value()) == "TRUE";
        if (std::find(added.begin(), added.end(), d.name) == added.end() || d.type == "NICK") {
            added.push_back(d.name);
            default_cards.push_back(d);
        }
    }
    return !stages.empty();
}

std::string Config::level_file(int stage, int level) const {
    if (stage < 1 || stage > static_cast<int>(stages.size())) return "palace_you_win.xml";
    return stages[stage - 1].name + "_" + std::to_string(level) + ".xml";
}

}  // namespace sbso::game
