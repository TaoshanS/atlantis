#include "database.h"
#include "core/vfs_xml.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

#include "pugixml.hpp"

namespace sbso::game {
namespace {

// AS3 int(String): leading integer or 0.
int as_int(const pugi::xml_attribute& a) { return a ? static_cast<int>(std::strtol(a.value(), nullptr, 10)) : 0; }

const char* const kBossList[] = {"bot_plankton_red", "bot_plankton", "bully_beach_atlantean", "bubble_dirty", "flying_dutchman",
                                 "atlantean_king", "bubble_dirty", "manray", "plankton"};
const char* const kFriends[] = {"sandy", "gary", "patrick", "squidward", "oldest_bubble", "buddy_bubble", "spongebob", "mr_krabs", "atlantean_friend"};

}  // namespace

CardType card_type_from_string(const std::string& s) {
    if (s == "ATTACK") return CardType::Attack;
    if (s == "DEFEND") return CardType::Defend;
    if (s == "NICK") return CardType::Nick;
    return CardType::Move;
}

const char* card_type_name(CardType t) {
    static const char* n[] = {"MOVE", "ATTACK", "DEFEND", "NICK"};
    return n[static_cast<int>(t)];
}

bool is_friend(const std::string& t) { return std::find(std::begin(kFriends), std::end(kFriends), t) != std::end(kFriends); }
bool has_boss_status(const std::string& t) { return std::find(std::begin(kBossList), std::end(kBossList), t) != std::end(kBossList); }
int boss_index(const std::string& t) {
    auto it = std::find(std::begin(kBossList), std::end(kBossList), t);
    return it == std::end(kBossList) ? -1 : static_cast<int>(it - std::begin(kBossList));
}
bool is_boss(const std::string& t) {
    auto it = std::find(std::begin(kBossList), std::end(kBossList), t);
    return it != std::end(kBossList) && (it - std::begin(kBossList)) > 1;
}

const Card* Database::card(const std::string& name) const {
    auto it = cards_.find(name);
    return it == cards_.end() ? nullptr : &it->second;
}

const EnemyDef& Database::enemy(const std::string& name) const {
    auto it = enemies_.find(name);
    if (it == enemies_.end()) it = enemies_.find("eel_blue");
    if (it == enemies_.end()) throw std::runtime_error("enemy database has no eel_blue fallback");
    return it->second;
}

bool Database::load(const std::string& dir, std::string* error) {
    pugi::xml_document doc;
    auto res = vfs::load_xml(doc, dir + "/cards.xml");
    if (!res) { if (error) *error = std::string("cards.xml: ") + res.description(); return false; }
    for (auto n : doc.child("cards").children("card")) {
        Card c;
        c.name = n.attribute("name").value();
        c.symbol = n.attribute("symbol").value();
        c.effect = n.attribute("effect").value();
        c.type = card_type_from_string(n.attribute("type").value());
        c.min = as_int(n.attribute("min"));
        c.max = as_int(n.attribute("max"));
        c.cost = as_int(n.attribute("cost"));
        if (c.type != CardType::Move) {
            c.damage = as_int(n.attribute("damage"));
            c.attack_gauge_seconds = n.attribute("attackGaugeSeconds").as_double();
        }
        if (c.type == CardType::Defend) {
            c.counter_gauge_sec = as_int(n.attribute("counterGaugeSec"));
            c.counter_damage = as_int(n.attribute("counterDamage"));
        }
        cards_[c.name] = c;
    }
    res = vfs::load_xml(doc, dir + "/enemies.xml");
    if (!res) { if (error) *error = std::string("enemies.xml: ") + res.description(); return false; }
    for (auto n : doc.child("enemies").children("enemy")) {
        EnemyDef e;
        e.name = n.attribute("name").value();
        e.scan_range = as_int(n.attribute("scanRange"));
        e.health = as_int(n.attribute("health"));
        e.energy = as_int(n.attribute("energy"));
        e.coins = as_int(n.attribute("coins"));
        for (auto c : n.child("cards").children("card")) {
            const Card* info = card(c.attribute("name").value());
            if (!info) { if (error) *error = "enemy card not in database: " + std::string(c.attribute("name").value()); return false; }
            e.cards[static_cast<int>(info->type)].push_back(c.attribute("name").value());
        }
        for (auto d : n.child("dropCards").children("card")) {
            e.drop_cards.push_back(d.text().get());
            e.has_drop_cards = true;
        }
        if (n.child("dropCards").child("empties")) e.drop_empties = n.child("dropCards").child("empties").text().as_int();
        enemies_[e.name] = e;
    }
    return true;
}

}  // namespace sbso::game
