#include "level.h"
#include "core/vfs_xml.h"

#include <cstdlib>
#include <sstream>

#include "pugixml.hpp"

namespace sbso::game {
namespace {

int as_int(const pugi::xml_attribute& a) { return a ? static_cast<int>(std::strtol(a.value(), nullptr, 10)) : 0; }

std::vector<int> csv(const char* s) {
    std::vector<int> v;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) v.push_back(static_cast<int>(std::strtol(tok.c_str(), nullptr, 10)));
    return v;
}

}  // namespace

bool load_level(const std::string& path, Level* out, std::string* error) {
    pugi::xml_document doc;
    auto res = vfs::load_xml(doc, path);
    if (!res) { if (error) *error = path + ": " + res.description(); return false; }
    auto root = doc.child("level");
    Level l;
    l.name = root.attribute("name").value();
    l.width = as_int(root.attribute("width"));
    l.height = as_int(root.attribute("height"));
    l.tileset = as_int(root.attribute("set"));
    l.collision = csv(root.child("layer").attribute("layer").value());
    l.tiles = csv(root.child("tiles").attribute("tiles").value());
    l.sb_x = as_int(root.child("spongebob").attribute("x"));
    l.sb_y = as_int(root.child("spongebob").attribute("y"));
    l.flag_x = as_int(root.child("flag").attribute("x"));
    l.flag_y = as_int(root.child("flag").attribute("y"));
    for (auto e : root.child("enemies").children("enemy")) {
        Spawn s;
        s.name = e.attribute("name").value();
        s.x = as_int(e.attribute("x"));
        s.y = as_int(e.attribute("y"));
        s.belt = std::string(e.attribute("belt").value()) == "true";
        s.effect = e.attribute("effect").value();
        s.scan_range = e.attribute("scanRange") ? as_int(e.attribute("scanRange")) : -1;
        auto mod = [&](const char* tag, bool* has, int* add, int* rnd) {
            auto n = e.child(tag);
            if (!n) return;
            *has = true;
            *add = as_int(n.attribute("add"));
            *rnd = as_int(n.attribute("rndrange"));
        };
        mod("coin", &s.has_coin, &s.coin_add, &s.coin_rnd);
        mod("energy", &s.has_energy, &s.energy_add, &s.energy_rnd);
        mod("health", &s.has_health, &s.health_add, &s.health_rnd);
        for (auto d : e.child("dropCards").children("card")) { s.drop_cards.push_back(d.text().get()); s.has_drop_cards = true; }
        if (e.child("dropCards").child("empties")) s.drop_empties = e.child("dropCards").child("empties").text().as_int();
        l.spawns.push_back(s);
    }
    if (l.width <= 0 || l.height <= 0 || static_cast<int>(l.collision.size()) < l.width * l.height) {
        if (error) *error = path + ": inconsistent level size";
        return false;
    }
    *out = std::move(l);
    return true;
}

}  // namespace sbso::game
