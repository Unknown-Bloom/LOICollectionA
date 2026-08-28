#pragma once

#include <string>

class Player;
class ItemStack;

namespace InventoryUtils {
    void clearItem(Player& player, const std::string& mTypeName, int mNumber);
    void clearItem(Player& player, const ItemStack& item, int mNumber);
    void giveItem(Player& player, const ItemStack& item, int mNumber);

    bool isItemInInventory(Player& player, const std::string& mTypeName, int mNumber);
    bool isItemInInventory(Player& player, const ItemStack& item, int mNumber);
}