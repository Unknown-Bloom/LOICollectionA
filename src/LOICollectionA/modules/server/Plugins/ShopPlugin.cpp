#include <atomic>
#include <memory>
#include <string>
#include <filesystem>

#include <fmt/core.h>

#include <ll/api/Expected.h>
#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>
#include <ll/api/command/Command.h>
#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>

#include <mc/deps/nbt/Tag.h>
#include <mc/deps/nbt/CompoundTag.h>

#include <mc/world/item/ItemStack.h>
#include <mc/world/actor/player/Player.h>

#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandOutput.h>
#include <mc/server/commands/CommandVersion.h>
#include <mc/server/commands/CommandPermissionLevel.h>

#include "LOICollectionA/include/server/Plugins/ChatPlugin.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/mc-server/InventoryUtils.h"
#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"

#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/ShopPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    struct ShopPlugin::operation {
        std::string Id;
    };

    struct ShopPlugin::Impl {
        std::atomic<bool> mRegistered{ false };

        bool ModuleEnabled = false;

        std::shared_ptr<ll::io::Logger> logger;

        std::filesystem::path mConfigPath;
        
        std::string mGuiFile;
    };

    ShopPlugin::ShopPlugin() : mImpl(std::make_unique<Impl>()) {};
    ShopPlugin::~ShopPlugin() = default;

    std::shared_ptr<ShopPlugin> ShopPlugin::getShared() {
        static auto instance = std::shared_ptr<ShopPlugin>(new ShopPlugin());
        return instance;
    }

    std::error_code ShopPlugin::makeErrorCode(ShopPluginErrorCode e) {
        static ShopPluginErrorCategory cat;
        return std::error_code{ static_cast<int>(e), cat };
    }

    std::shared_ptr<ll::io::Logger> ShopPlugin::getLogger() {
        return this->mImpl->logger;
    }

    void ShopPlugin::registeryCommand() {
        ll::command::CommandHandle& command = ll::command::CommandRegistrar::getInstance(false)
            .getOrCreateCommand("shop", tr({}, "commands.shop.description"), CommandPermissionLevel::Any, CommandFlagValue::NotCheat | CommandFlagValue::Async);
        command.overload<operation>().text("gui").required("Id").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                Actor* entity = origin.getEntity();
                if (entity == nullptr || !entity->isType(ActorType::Player))
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
                Player& player = *static_cast<Player*>(entity);

                this->open(player, param.Id).or_else(modules::defaultErrorHandler<ShopPlugin>);

                output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
            });
        command.overload().text("reload").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

            output.success(tr(origin.getLocaleCode(), "commands.generic.reload"));
            
            this->registeryUI()
                .transform([&origin, &output]() -> void {
                    output.success(tr(origin.getLocaleCode(), "commands.generic.reload.success"));
                })
                .or_else(modules::defaultErrorHandler<ShopPlugin>);
        });
    }

    ll::Expected<void> ShopPlugin::registeryUI() {
        auto resolve = [this](const std::string& file) -> std::filesystem::path {
            std::filesystem::path path(file);
            return path.is_absolute() ? path : this->mImpl->mConfigPath / path;
        };

        return form::GUIManager::getInstance().load("shop", resolve(this->mImpl->mGuiFile).string());
    }

    ll::Expected<void> ShopPlugin::open(Player& player, const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(ShopPluginErrorCode::Invalid));

        return form::GUIManager::getInstance().open("shop", id, form::GUIManagerType::ScriptForm, player);
    }

    ll::Expected<ShopActionResult> ShopPlugin::commodity(Player& player, int number, const ShopItemData& data, ShopType type) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(ShopPluginErrorCode::Invalid));

        if (number <= 0 || number > 2304)
            return ShopActionResult::InvalidNumber;

        if (data.type.empty())
            return ShopActionResult::MissingItem;

        if (type == ShopType::buy) {
            return this->checkModifiedData(player, data, number)
                .transform([number, &data, &player](bool exists) -> ShopActionResult {
                    if (!exists)
                        return ShopActionResult::InsufficientScore;

                    auto itemStack = std::make_unique<ItemStack>();
                    if (!data.nbt.empty())
                        itemStack = std::make_unique<ItemStack>(ItemStack::fromTag(CompoundTag::fromSnbt(data.nbt)->mTags));
                    else
                        itemStack->reinit(data.id, 1, 0);

                    InventoryUtils::giveItem(player, *itemStack, number);
                    player.refreshInventory();

                    return ShopActionResult::Success;
                });
        }

        if (!InventoryUtils::isItemInInventory(player, data.id, number))
            return ShopActionResult::MissingItem;

        for (const auto& score : data.scores)
            ScoreboardUtils::addScore(player, score.objective, score.value * number);

        InventoryUtils::clearItem(player, data.id, number);
        player.refreshInventory();

        return ShopActionResult::Success;
    }

    ll::Expected<ShopActionResult> ShopPlugin::title(Player& player, const ShopItemData& data, ShopType type) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(ShopPluginErrorCode::Invalid));

        if (data.type.empty())
            return ShopActionResult::MissingTitle;

        std::string id = data.id.empty() ? "None" : data.id;

        if (type == ShopType::buy) {
            return this->checkModifiedData(player, data, 1)
                .and_then([id, &data, &player](bool exists) -> ll::Expected<ShopActionResult> {
                    if (!exists)
                        return ShopActionResult::InsufficientScore;

                    auto result = data.time > 0
                        ? ChatPlugin::getShared()->addTitle(player, id, data.time)
                        : ChatPlugin::getShared()->addTitle(player, id, 0);

                    if (!result.has_value()) {
                        if (result.error().isA<ll::ErrorCodeError>() && result.error().as<ll::ErrorCodeError>().ec == ChatPlugin::makeErrorCode(ChatPluginErrorCode::Invalid))
                            return ShopActionResult::Success;

                        return ll::Unexpected(result.error());
                    }

                    return ShopActionResult::Success;
                });
        }

        return ChatPlugin::getShared()->hasTitle(player, id)
            .or_else([](ll::Error e) -> ll::Expected<bool> {
                if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == ChatPlugin::makeErrorCode(ChatPluginErrorCode::Invalid))
                    return false;

                return ll::Unexpected(e);
            })
            .and_then([id, &data, &player](bool exists) -> ll::Expected<ShopActionResult> {
                if (!exists)
                    return ShopActionResult::MissingTitle;

                for (const auto& score : data.scores)
                    ScoreboardUtils::addScore(player, score.objective, score.value);

                auto delResult = ChatPlugin::getShared()->delTitle(player, id);
                if (!delResult.has_value()) {
                    if (delResult.error().isA<ll::ErrorCodeError>() && delResult.error().as<ll::ErrorCodeError>().ec == ChatPlugin::makeErrorCode(ChatPluginErrorCode::Invalid))
                        return ShopActionResult::Success;

                    return ll::Unexpected(delResult.error());
                }

                return ShopActionResult::Success;
            });
    }

    ll::Expected<bool> ShopPlugin::checkModifiedData(Player& player, const ShopItemData& data, int number) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(ShopPluginErrorCode::Invalid));

        if (data.scores.empty())
            return true;

        for (const auto& score : data.scores) {
            if (score.value * number > ScoreboardUtils::getScore(player, score.objective))
                return false;
        }

        for (const auto& score : data.scores)
            ScoreboardUtils::reduceScore(player, score.objective, score.value * number);

        return true;
    }

    bool ShopPlugin::isValid() {
        return this->getLogger() != nullptr && this->mImpl->ModuleEnabled;
    }

    std::string ShopPlugin::getName() {
        return "ShopPlugin";
    }

    modules::ModulePriority ShopPlugin::getPriority() {
        return modules::ModulePriority::High;
    }

    ll::Expected<bool> ShopPlugin::load() {
        if (!ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Shop.ModuleEnabled)
            return false;

        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        this->mImpl->ModuleEnabled = true;
        this->mImpl->mConfigPath = std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("ConfigPath")->data());
        this->mImpl->mGuiFile = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Shop.GuiPath;

        return true;
    }

    ll::Expected<bool> ShopPlugin::unload() {
        if (!this->mImpl->ModuleEnabled)
            return false;

        this->mImpl->logger.reset();
        this->mImpl->ModuleEnabled = false;

        return true;
    }

    ll::Expected<bool> ShopPlugin::registry() {
        if (!this->mImpl->ModuleEnabled)
            return false;

        return this->registeryUI()
            .transform([this]() -> bool {
                this->registeryCommand();

                this->mImpl->mRegistered.store(true, std::memory_order_release);

                return true;
            });
    }

    ll::Expected<bool> ShopPlugin::unregistry() {
        if (!this->mImpl->ModuleEnabled)
            return false;

        this->mImpl->mRegistered.store(false, std::memory_order_release);

        return true;
    }
}
