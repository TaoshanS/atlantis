// Level files (maps/<stage>_<n>.xml) as read by Map.buildMap in the original.
#pragma once
#include <string>
#include <vector>

namespace sbso::game {

struct Spawn {
    std::string name;
    int x = 0, y = 0;
    bool belt = false;
    std::string effect;   // "exit" marks units on the bubble-off list
    int scan_range = -1;  // -1: use the enemy definition
    int coin_add = 0, coin_rnd = 0, energy_add = 0, energy_rnd = 0, health_add = 0, health_rnd = 0;
    bool has_coin = false, has_energy = false, has_health = false;
    std::vector<std::string> drop_cards;
    bool has_drop_cards = false;
    int drop_empties = 0;
};

struct Level {
    std::string name;
    int width = 0, height = 0, tileset = 0;
    std::vector<int> tiles;      // tile frame per cell
    std::vector<int> collision;  // 0 = blocked, otherwise a bitmask of "elevation layers"
    int sb_x = 0, sb_y = 0;
    int flag_x = 0, flag_y = 0;
    std::vector<Spawn> spawns;
};

bool load_level(const std::string& path, Level* out, std::string* error);

}  // namespace sbso::game
