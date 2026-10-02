#include "save_data.h"

#include <algorithm>

namespace sbso::save {
namespace {

int type_index(const std::string& t) {
    if (t == "MOVE") return 0;
    if (t == "ATTACK") return 1;
    if (t == "DEFEND") return 2;
    return 3;
}

}  // namespace

Progress new_progress(const std::string& name, const std::vector<DefaultCard>& defaults) {
    Progress p;
    p.name = name.substr(0, kMaxProfileName);
    for (const DefaultCard& c : defaults) {
        int t = type_index(c.type);
        p.all_cards[t].push_back(c.name);
        if (c.in_belt) p.belted[t].push_back(c.name);
    }
    for (int t = 0; t < kNumCardTypes; ++t) {
        std::sort(p.all_cards[t].begin(), p.all_cards[t].end());
        std::sort(p.belted[t].begin(), p.belted[t].end());
    }
    p.gained_belts[0] = 1;
    p.unlocked_stages[0][0] = kUnlocked;
    p.coins = 3;
    return p;
}

void add_coins(Progress& p, int delta) { p.coins = std::max(0, std::min(999, p.coins + delta)); }

void add_new_card(Progress& p, int t, const std::string& card) {
    p.new_cards[t].push_back(card);
    p.all_cards[t].insert(p.all_cards[t].begin(), card);
}

}  // namespace sbso::save
