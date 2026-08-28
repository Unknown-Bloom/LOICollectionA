#include <string>

#include <mc/world/item/ItemStack.h>
#include <mc/world/actor/player/Player.h>
#include <mc/world/actor/player/PlayerInventory.h>
#include <mc/world/actor/player/Inventory.h>

#include <mc/util/LootTableUtils.h>

#include "LOICollectionA/utils/mc-server/InventoryUtils.h"

namespace InventoryUtils {
    void clearItem(Player& player, const std::string& mTypeName, int mNumber) {
        auto& inventory = player.mInventory->mInventory;
        for (int i = 0; i < inventory->getContainerSize() && mNumber > 0; ++i) {
            const ItemStack& mItemObject = inventory->getItem(i);
            
            if ((mItemObject || !mItemObject.isNull()) && mItemObject.getTypeName() == mTypeName) {
                int mCount = std::min(static_cast<int>(mItemObject.mCount), mNumber);

                inventory->removeItem(i, mCount);

                mNumber -= mCount;
            }
        }
    }

    void clearItem(Player& player, const ItemStack& item, int mNumber) {
        auto& inventory = player.mInventory->mInventory;
        for (int i = 0; i < inventory->getContainerSize() && mNumber > 0; ++i) {
            const ItemStack& mItemObject = inventory->getItem(i);

            if (mItemObject.matches(item)) {
                int mCount = std::min(static_cast<int>(mItemObject.mCount), mNumber);

                inventory->removeItem(i, mCount);

                mNumber -= mCount;
            }
        }
    }

    void giveItem(Player& player, ItemStack& item, int mNumber) {
        std::vector<ItemStack> mItemStacks{};
        for (int count; mNumber > 0; mNumber -= count)
            mItemStacks.emplace_back(item).mCount = static_cast<uchar>(count = std::min(mNumber, 64));

        Util::LootTableUtils::givePlayer(player, mItemStacks, true);
    }

    bool isItemInInventory(Player& player, const std::string& mTypeName, int mNumber) {
        if (mTypeName.empty() || mNumber <= 0)
            return false;
        
        auto& mItemInventory = player.mInventory->mInventory;
        for (int i = 0; i < mItemInventory->getContainerSize() && mNumber > 0; ++i) {
            const ItemStack& mItemObject = mItemInventory->getItem(i);
            
            if ((mItemObject || !mItemObject.isNull()) && mTypeName == mItemObject.getTypeName())
                mNumber -= static_cast<int>(mItemObject.mCount);
        }

        return mNumber <= 0;
    }

    bool isItemInInventory(Player& player, const ItemStack& item, int mNumber) {
        if (mNumber <= 0)
            return false;

        auto& mItemInventory = player.mInventory->mInventory;
        for (int i = 0; i < mItemInventory->getContainerSize() && mNumber > 0; ++i) {
            const ItemStack& mItemObject = mItemInventory->getItem(i);

            if (mItemObject.matches(item))
                mNumber -= static_cast<int>(mItemObject.mCount);
        }

        return mNumber <= 0;
    }
}