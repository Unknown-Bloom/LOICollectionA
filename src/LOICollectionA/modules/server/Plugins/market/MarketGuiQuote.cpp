#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <optional>
#include <unordered_map>

#include <fmt/core.h>

#include <ll/api/Expected.h>

#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

#include "LOICollectionA/utils/I18nUtils.h"

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/include/server/Plugins/market/MarketGui.h"
#include "LOICollectionA/include/server/Plugins/market/MarketPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    void MarketGui::registerQuote(MarketPlugin& owner) {
        auto listTopQuoteItems = [&owner](int limit) -> ll::Expected<std::vector<std::pair<std::string, long long>>> {
            return owner.getTopVolume(limit, 7);
        };

        form::GUIManager::getInstance().registerRequest("market.quote.enabled", [&owner](frontend::ArrayRef, Player&) -> ll::Expected<frontend::ArrayRef> {
            auto values = std::make_shared<frontend::ArrayValue>();
            values->elements.emplace_back(owner.getOptions().StoreQuoteEnabled);

            return values;
        });

        form::GUIManager::getInstance().registerValue("market.quote.items", [listTopQuoteItems](Player& player) -> ll::Expected<frontend::ArrayRef> {
            return LanguagePlugin::getShared()->getLanguage(player)
                .and_then([listTopQuoteItems, &player](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                    return listTopQuoteItems(50)
                        .transform([language](const std::vector<std::pair<std::string, long long>>& items) -> frontend::ArrayRef {
                            auto values = std::make_shared<frontend::ArrayValue>();

                            for (const auto& [name, count] : items)
                                values->elements.emplace_back(fmt::format(fmt::runtime(tr(language, "market.gui.quote.line")), name, count));

                            return values;
                        });
                });
        });

        form::GUIManager::getInstance().registerRequest("market.quote.id", [listTopQuoteItems](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                return ll::makeStringError("market.quote.id: must take exactly one int parameter");

            int index = std::get<int>(args->elements[0]);

            return listTopQuoteItems(50)
                .and_then([index](const std::vector<std::pair<std::string, long long>>& items) -> ll::Expected<frontend::ArrayRef> {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    if (index < 0 || index >= static_cast<int>(items.size()))
                        return values;

                    values->elements.emplace_back(items.at(static_cast<size_t>(index)).first);

                    return values;
                });
        });

        form::GUIManager::getInstance().registerRequest("market.quote.info", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                return ll::makeStringError("market.quote.info: must take exactly one string parameter");

            std::string itemName = std::get<std::string>(args->elements[0]);
            auto values = std::make_shared<frontend::ArrayValue>();

            return LanguagePlugin::getShared()->getLanguage(player)
                .and_then([&owner, itemName, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                    return owner.getQuote(itemName)
                        .transform([language, itemName, values](const std::optional<QuoteInfo>& info) -> frontend::ArrayRef {
                            if (!info.has_value()) {
                                values->elements.emplace_back(tr(language, "market.gui.quote.empty"));

                                return values;
                            }

                            values->elements.emplace_back(fmt::format(
                                fmt::runtime(tr(language, "market.gui.quote.info")),
                                itemName,
                                info->avg7d, info->avg30d, info->min30d, info->max30d, info->count30d, info->lastPrice
                            ));

                            return values;
                        });
                });
        });
    }
}
