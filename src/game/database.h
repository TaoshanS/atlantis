// Static game data loaded from maps/cards.xml and maps/enemies.xml (same files as the original).
#pragma once
#include <map>
#include <string>
#include <vector>

namespace sbso::game {

enum class CardType { Move = 0, Attack = 1, Defend = 2, Nick = 3 };
constexpr int kNumCardTypes = 4;

CardType card_type_from_string(const std::string& s);
const char* card_type_name(CardType t);

struct Card {
    std::string name, symbol, effect;
    CardType type = CardType::Move;
    int min = 0, max = 0, cost = 0;
    int damage = 0;               // not present on MOVE cards
    int counter_gauge_sec = 0;    // DEFEND cards only (as parseInt in the original)
    int counter_damage = 0;       // DEFEND cards only
    double attack_gauge_seconds = 0;
};

struct EnemyDef {
    std::string name;
    int scan_range = 0, health = 0, energy = 0, coins = 0;
    std::vector<std::string> cards[kNumCardTypes];
    std::vector<std::string> drop_cards;
    bool has_drop_cards = false;
    int drop_empties = 0;
};

class Database {
public:
    // dir is the "maps" directory of the original game. Returns false and sets error on failure.
    bool load(const std::string& dir, std::string* error);

    const Card* card(const std::string& name) const;
    // Like GameInfo.getEnemyInfo: unknown enemies fall back to "eel_blue".
    const EnemyDef& enemy(const std::string& name) const;
    const std::map<std::string, Card>& cards() const { return cards_; }
    const std::map<std::string, EnemyDef>& enemies() const { return enemies_; }

private:
    std::map<std::string, Card> cards_;
    std::map<std::string, EnemyDef> enemies_;
};

// Static lists from GameInfo.as.
bool is_friend(const std::string& type);         // sDontHitYourFriendsList
bool has_boss_status(const std::string& type);   // sBosslist.indexOf != -1
bool is_boss(const std::string& type);           // sBosslist.indexOf > 1 (quirk kept)
int boss_index(const std::string& type);         // sBosslist.indexOf (first match) or -1

}  // namespace sbso::game
