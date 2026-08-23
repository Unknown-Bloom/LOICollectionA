#pragma once

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <fmt/core.h>

#include <ll/api/Expected.h>
#include <ll/api/service/Bedrock.h>

#include <mc/world/actor/player/Player.h>
#include <mc/world/actor/player/PlayerInventory.h>
#include <mc/world/actor/player/Inventory.h>
#include <mc/world/item/ItemStack.h>

#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

#include "LOICollectionA/utils/I18nUtils.h"

#include "LOICollectionA/include/server/Plugins/market/MarketPlugin.h"

namespace LOICollection::server::Plugins::marketGui {
    using I18nUtilsTools::tr;

    // 当前在线玩家（排除自己与模拟玩家），供玩家选择列表使用
    inline std::vector<std::pair<std::string, std::string>> listPlayers(Player& player) {
        std::vector<std::pair<std::string, std::string>> players;

        ll::service::getLevel()->forEachPlayer([&player, &players](Player& target) -> bool {
            if (target.isSimulatedPlayer() || target.getUuid() == player.getUuid())
                return true;

            players.emplace_back(target.getRealName(), target.getUuid().asString());
            return true;
        });

        return players;
    }

    // 背包中可上架的物品（排除禁售物品），供寄售/交易/上架列表使用
    inline ll::Expected<std::vector<std::pair<std::string, int>>> listSellableInventory(MarketPlugin& owner, Player& player) {
        return LanguagePlugin::getShared()->getLanguage(player)
            .transform([&owner, &player](const std::string& language) -> std::vector<std::pair<std::string, int>> {
                std::vector<std::pair<std::string, int>> items;
                std::vector<std::string> prohibitedItems = owner.getProhibitedItems();

                for (int i = 0; i < player.mInventory->mInventory->getContainerSize(); i++) {
                    ItemStack mItemStack = player.mInventory->mInventory->getItem(i);

                    if (!mItemStack || mItemStack.isNull() ||
                        std::find(prohibitedItems.begin(), prohibitedItems.end(), mItemStack.getTypeName()) != prohibitedItems.end())
                        continue;

                    items.emplace_back(fmt::format(fmt::runtime(tr(language, "market.gui.sell.item.text")),
                        mItemStack.getName(), std::to_string(mItemStack.mCount)
                    ), i);
                }

                return items;
            });
    }
}
