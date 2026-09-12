#include <atomic>
#include <memory>
#include <string>
#include <filesystem>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <ll/api/Expected.h>
#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>
#include <ll/api/command/Command.h>
#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>

#include <mc/deps/nbt/Tag.h>
#include <mc/deps/nbt/CompoundTag.h>

#include <mc/world/item/ItemStack.h>
#include <mc/world/item/SaveContext.h>
#include <mc/world/item/SaveContextFactory.h>

#include <mc/world/actor/player/Player.h>
#include <mc/world/actor/player/PlayerInventory.h>
#include <mc/world/actor/player/Inventory.h>

#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandOutput.h>
#include <mc/server/commands/CommandPermissionLevel.h>

#include <mc/safety/RedactableString.h>

#include "LOICollectionA/include/CallbackUtils.h"
#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"
#include "LOICollectionA/include/server/Plugins/ChatPlugin.h"

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/mc-server/InventoryUtils.h"
#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/JsonStorage.h"

#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/CdkPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    struct CdkPlugin::operation {
        std::string Id;
    };

    struct CdkPlugin::Impl {
        std::atomic<bool> mRegistered{ false };

        bool ModuleEnabled = false;
        
        std::shared_ptr<JsonStorage> db;
        std::shared_ptr<ll::io::Logger> logger;

        std::string mGuiPath;
    };

    CdkPlugin::CdkPlugin() : mImpl(std::make_unique<Impl>()) {};
    CdkPlugin::~CdkPlugin() = default;

    std::shared_ptr<CdkPlugin> CdkPlugin::getShared() {
        static auto instance = std::shared_ptr<CdkPlugin>(new CdkPlugin());
        return instance;
    }

    std::error_code CdkPlugin::makeErrorCode(CdkPluginErrorCode e) {
        static CdkPluginErrorCategory cat;
        return std::error_code{ static_cast<int>(e), cat };
    }

    std::shared_ptr<JsonStorage> CdkPlugin::getDatabase() {
        return this->mImpl->db;
    }

    std::shared_ptr<ll::io::Logger> CdkPlugin::getLogger() {
        return this->mImpl->logger;
    }

    void CdkPlugin::registeryCommand() {
        ll::command::CommandHandle& command = ll::command::CommandRegistrar::getInstance(false)
            .getOrCreateCommand("cdk", tr({}, "commands.cdk.description"), CommandPermissionLevel::Any, CommandFlagValue::NotCheat | CommandFlagValue::Async);
        command.overload<operation>().text("convert").required("Id").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                Actor* entity = origin.getEntity();
                if (entity == nullptr || !entity->isType(ActorType::Player))
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
                Player& player = *static_cast<Player*>(entity);

                this->convert(player, param.Id)
                    .or_else([](ll::Error e) -> ll::Expected<void> {
                        if (e.isA<ll::ErrorCodeError>() 
                            && (e.as<ll::ErrorCodeError>().ec == makeErrorCode(CdkPluginErrorCode::NotFound)
                                || e.as<ll::ErrorCodeError>().ec == makeErrorCode(CdkPluginErrorCode::Received)))
                            return {};

                        return ll::Unexpected(e);
                    })
                    .transform([&output, &origin, name = player.getRealName(), id = param.Id]() -> void {
                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.cdk.success.convert")), name, id);
                    })
                    .or_else(modules::defaultErrorHandler<CdkPlugin>);
            });
        command.overload().text("gui").execute([](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            form::GUIManager::getInstance().open("cdk", "cdk.convert", form::GUIManagerType::CustomForm, player)
                .or_else(modules::defaultErrorHandler<CdkPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("reload").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

            output.success(tr(origin.getLocaleCode(), "commands.generic.reload"));
            
            form::GUIManager::getInstance().load("cdk", this->mImpl->mGuiPath)
                .transform([&origin, &output]() -> void {
                    output.success(tr(origin.getLocaleCode(), "commands.generic.reload.success"));
                })
                .or_else(modules::defaultErrorHandler<CdkPlugin>);
        });
        command.overload().text("edit").execute([](CommandOrigin const& origin, CommandOutput& output) -> void {
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));
            
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            form::GUIManager::getInstance().open("cdk", "cdk.open", form::GUIManagerType::CustomForm, player)
                .or_else(modules::defaultErrorHandler<CdkPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
    }

    ll::Expected<void> CdkPlugin::registeryUI() {
        return form::GUIManager::getInstance().load("cdk", this->mImpl->mGuiPath)
            .transform([this]() -> void {
                form::GUIManager::getInstance().registerValue("cdk.cdks", [this](Player&) -> ll::Expected<frontend::ArrayRef> {
                    return this->getCdks()
                        .transform([](const std::vector<std::string>& cdks) -> frontend::ArrayRef {
                            auto values = std::make_shared<frontend::ArrayValue>();

                            for (const std::string& id : cdks)
                                values->elements.emplace_back(id);

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerValue("cdk.inventory", [](Player& player) -> ll::Expected<frontend::ArrayRef> {
                    return LanguagePlugin::getShared()->getLanguage(player)
                        .transform([&player](const std::string& language) -> frontend::ArrayRef {
                            auto values = std::make_shared<frontend::ArrayValue>();

                            for (int i = 0; i < player.mInventory->mInventory->getContainerSize(); i++) {
                                ItemStack mItemStack = player.mInventory->mInventory->getItem(i);

                                if (!mItemStack || mItemStack.isNull())
                                    continue;

                                values->elements.emplace_back(fmt::format(fmt::runtime(tr(language, "cdk.gui.award.item.inventory.text")),
                                    mItemStack.getName(), std::to_string(mItemStack.mCount)
                                ));
                            }

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerRequest("cdk.info", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("cdk.info: must take exactly one string parameter");

                    auto id = std::get<std::string>(args->elements[0]);

                    return LanguagePlugin::getShared()->getLanguage(player)
                        .and_then([this, id, &player](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                            return this->has(id)
                                .and_then([this, language, id, &player](bool exists) -> ll::Expected<frontend::ArrayRef> {
                                    auto values = std::make_shared<frontend::ArrayValue>();

                                    if (!exists) {
                                        player.sendMessage(tr(language, "cdk.gui.error"));

                                        return values;
                                    }

                                    values->elements.emplace_back(fmt::format(fmt::runtime(tr(language, "cdk.gui.award.info.label")), id,
                                        this->getDatabase()->get_ptr<bool>("/" + id + "/personal").value_or(false) ? "true" : "false",
                                        SystemUtils::toFormatTime(this->getDatabase()->get_ptr<std::string>("/" + id + "/time").value_or("None"), "None")
                                    ));

                                    return values;
                                });
                        });
                });

                form::GUIManager::getInstance().registerRequest("cdk.inventory.slot", [](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                        return ll::makeStringError("cdk.inventory.slot: must take exactly one int parameter");

                    int index = std::get<int>(args->elements[0]);
                    std::vector<int> slots;

                    for (int i = 0; i < player.mInventory->mInventory->getContainerSize(); i++) {
                        ItemStack mItemStack = player.mInventory->mInventory->getItem(i);

                        if (!mItemStack || mItemStack.isNull())
                            continue;

                        slots.push_back(i);
                    }

                    if (index < 0 || index >= static_cast<int>(slots.size()))
                        return ll::makeStringError("cdk.inventory.slot: index out of range");

                    auto values = std::make_shared<frontend::ArrayValue>();
                    values->elements.emplace_back(slots.at(static_cast<size_t>(index)));

                    return values;
                });

                form::GUIManager::getInstance().registerRequest("cdk.inventory.item", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 2 ||
                        !std::holds_alternative<std::string>(args->elements[0]) ||
                        !std::holds_alternative<int>(args->elements[1]))
                        return ll::makeStringError("cdk.inventory.item: must take exactly one string and one int parameter");

                    auto id = std::get<std::string>(args->elements[0]);
                    int slot = std::get<int>(args->elements[1]);

                    return LanguagePlugin::getShared()->getLanguage(player)
                        .and_then([this, id, slot, &player](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                            return this->has(id)
                                .and_then([language, id, slot, &player](bool exists) -> ll::Expected<frontend::ArrayRef> {
                                    auto values = std::make_shared<frontend::ArrayValue>();

                                    if (!exists) {
                                        player.sendMessage(tr(language, "cdk.gui.error"));

                                        return values;
                                    }

                                    ItemStack mItemStack = player.mInventory->mInventory->getItem(slot);
                                    if (!mItemStack || mItemStack.isNull()) {
                                        player.sendMessage(tr(language, "cdk.gui.error"));

                                        return values;
                                    }

                                    values->elements.emplace_back(fmt::format(fmt::runtime(tr(language, "cdk.gui.award.item.inventory.introduce")),
                                        mItemStack.getName(),
                                        mItemStack.mCount,
                                        mItemStack.save(*SaveContextFactory::createCloneSaveContext())->toSnbt(SnbtFormat::Minimize, 0)
                                    ));

                                    return values;
                                });
                        });
                });

                form::GUIManager::getInstance().registerCallback("cdk.convert.submit", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("cdk.convert.submit: must take exactly one string parameter");

                    std::string mCdk = std::get<std::string>(args->elements[0]);
                    if (mCdk.empty()) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                player.sendMessage(tr(language, "generic.tips.noinput"));

                                return {};
                            });
                    }

                    return this->convert(player, mCdk)
                        .or_else([](ll::Error e) -> ll::Expected<void> {
                            if (e.isA<ll::ErrorCodeError>()
                                && (e.as<ll::ErrorCodeError>().ec == makeErrorCode(CdkPluginErrorCode::NotFound)
                                    || e.as<ll::ErrorCodeError>().ec == makeErrorCode(CdkPluginErrorCode::Received)))
                                return {};

                            return ll::Unexpected(e);
                        });
                });

                form::GUIManager::getInstance().registerCallback("cdk.new.submit", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 3 ||
                        !std::holds_alternative<std::string>(args->elements[0]) ||
                        !std::holds_alternative<std::string>(args->elements[1]) ||
                        !std::holds_alternative<bool>(args->elements[2]))
                        return ll::makeStringError("cdk.new.submit: must take two string and one bool parameters");

                    std::string mObjectCdk = std::get<std::string>(args->elements[0]);
                    if (mObjectCdk.empty()) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                player.sendMessage(tr(language, "generic.tips.noinput"));

                                return {};
                            });
                    }

                    return this->create(
                        mObjectCdk,
                        SystemUtils::toInt(std::get<std::string>(args->elements[1]), 0),
                        std::get<bool>(args->elements[2])
                    ).transform([this, &player, mObjectCdk]() -> void {
                        this->getLogger()->info(fmt::runtime(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "cdk.log1"), player)), mObjectCdk);
                    });
                });

                form::GUIManager::getInstance().registerCallback("cdk.remove", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("cdk.remove: must take exactly one string parameter");

                    std::string id = std::get<std::string>(args->elements[0]);

                    return this->has(id)
                        .and_then([this, id, &player](bool exists) -> ll::Expected<void> {
                            if (!exists) {
                                return LanguagePlugin::getShared()->getLanguage(player)
                                    .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                        player.sendMessage(tr(language, "cdk.gui.error"));

                                        return {};
                                    });
                            }

                            return this->remove(id)
                                .transform([this, id, &player]() -> void {
                                    this->getLogger()->info(fmt::runtime(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "cdk.log2"), player)), id);
                                });
                        });
                });

                form::GUIManager::getInstance().registerCallback("cdk.award.score.submit", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 3 ||
                        !std::holds_alternative<std::string>(args->elements[0]) ||
                        !std::holds_alternative<std::string>(args->elements[1]) ||
                        !std::holds_alternative<std::string>(args->elements[2]))
                        return ll::makeStringError("cdk.award.score.submit: must take exactly three string parameters");

                    std::string id = std::get<std::string>(args->elements[0]);
                    std::string mObjective = std::get<std::string>(args->elements[1]);
                    int mScore = SystemUtils::toInt(std::get<std::string>(args->elements[2]), 0);

                    return this->has(id)
                        .and_then([this, id, mObjective, mScore, &player](bool exists) -> ll::Expected<void> {
                            if (!exists) {
                                return LanguagePlugin::getShared()->getLanguage(player)
                                    .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                        player.sendMessage(tr(language, "cdk.gui.error"));

                                        return {};
                                    });
                            }

                            if (mObjective.empty() || !ScoreboardUtils::hasScoreboard(mObjective)) {
                                return LanguagePlugin::getShared()->getLanguage(player)
                                    .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                        player.sendMessage(tr(language, "generic.tips.noinput"));

                                        return {};
                                    });
                            }

                            this->getDatabase()->set_ptr("/" + id + "/scores/" + mObjective, mScore);

                            return this->getDatabase()->save();
                        });
                });

                form::GUIManager::getInstance().registerCallback("cdk.award.item.submit", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 6 ||
                        !std::holds_alternative<std::string>(args->elements[0]) ||
                        !std::holds_alternative<std::string>(args->elements[1]) ||
                        !std::holds_alternative<std::string>(args->elements[2]) ||
                        !std::holds_alternative<std::string>(args->elements[3]) ||
                        !std::holds_alternative<std::string>(args->elements[4]) ||
                        !std::holds_alternative<std::string>(args->elements[5]))
                        return ll::makeStringError("cdk.award.item.submit: must take exactly six string parameters");

                    std::string id = std::get<std::string>(args->elements[0]);
                    std::string type = std::get<std::string>(args->elements[1]);
                    std::string mObjectId = std::get<std::string>(args->elements[2]);

                    return this->has(id)
                        .and_then([this, id, type, mObjectId, args, &player](bool exists) -> ll::Expected<void> {
                            if (!exists) {
                                return LanguagePlugin::getShared()->getLanguage(player)
                                    .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                        player.sendMessage(tr(language, "cdk.gui.error"));

                                        return {};
                                    });
                            }

                            if (mObjectId.empty()) {
                                return LanguagePlugin::getShared()->getLanguage(player)
                                    .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                        player.sendMessage(tr(language, "generic.tips.noinput"));

                                        return {};
                                    });
                            }

                            nlohmann::ordered_json mItemData = {
                                { "id", mObjectId },
                                { "type", type }
                            };

                            if (type == "universal") {
                                mItemData["name"] = std::get<std::string>(args->elements[3]);
                                mItemData["quantity"] = SystemUtils::toInt(std::get<std::string>(args->elements[4]), 1);
                                mItemData["specialvalue"] = SystemUtils::toInt(std::get<std::string>(args->elements[5]), 0);
                            }

                            int mIndex = static_cast<int>(this->getDatabase()->get_ptr<nlohmann::ordered_json>("/" + id + "/item").value_or(nlohmann::ordered_json::array()).size());

                            this->getDatabase()->set_ptr("/" + id + "/item/" + std::to_string(mIndex), mItemData);

                            return this->getDatabase()->save();
                        });
                });

                form::GUIManager::getInstance().registerCallback("cdk.award.inventory.submit", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 2 ||
                        !std::holds_alternative<std::string>(args->elements[0]) ||
                        !std::holds_alternative<int>(args->elements[1]))
                        return ll::makeStringError("cdk.award.inventory.submit: must take exactly one string and one int parameter");

                    std::string id = std::get<std::string>(args->elements[0]);
                    int slot = std::get<int>(args->elements[1]);

                    return this->has(id)
                        .and_then([this, id, slot, &player](bool exists) -> ll::Expected<void> {
                            if (!exists) {
                                return LanguagePlugin::getShared()->getLanguage(player)
                                    .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                        player.sendMessage(tr(language, "cdk.gui.error"));

                                        return {};
                                    });
                            }

                            ItemStack mItemStack = player.mInventory->mInventory->getItem(slot);
                            if (!mItemStack || mItemStack.isNull()) {
                                return LanguagePlugin::getShared()->getLanguage(player)
                                    .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                        player.sendMessage(tr(language, "cdk.gui.error"));

                                        return {};
                                    });
                            }

                            nlohmann::ordered_json mItemData = {
                                { "id", mItemStack.save(*SaveContextFactory::createCloneSaveContext())->toSnbt(SnbtFormat::Minimize, 0) },
                                { "type", "nbt" }
                            };

                            int mIndex = static_cast<int>(this->getDatabase()->get_ptr<nlohmann::ordered_json>("/" + id + "/item").value_or(nlohmann::ordered_json::array()).size());

                            this->getDatabase()->set_ptr("/" + id + "/item/" + std::to_string(mIndex), mItemData);

                            return this->getDatabase()->save();
                        });
                });

                form::GUIManager::getInstance().registerCallback("cdk.award.title.submit", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 3 ||
                        !std::holds_alternative<std::string>(args->elements[0]) ||
                        !std::holds_alternative<std::string>(args->elements[1]) ||
                        !std::holds_alternative<std::string>(args->elements[2]))
                        return ll::makeStringError("cdk.award.title.submit: must take exactly three string parameters");

                    std::string id = std::get<std::string>(args->elements[0]);
                    std::string mObjectTitle = std::get<std::string>(args->elements[1]);
                    int mObjectData = SystemUtils::toInt(std::get<std::string>(args->elements[2]), 0);

                    return this->has(id)
                        .and_then([this, id, mObjectTitle, mObjectData, &player](bool exists) -> ll::Expected<void> {
                            if (!exists) {
                                return LanguagePlugin::getShared()->getLanguage(player)
                                    .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                        player.sendMessage(tr(language, "cdk.gui.error"));

                                        return {};
                                    });
                            }

                            if (mObjectTitle.empty()) {
                                return LanguagePlugin::getShared()->getLanguage(player)
                                    .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                        player.sendMessage(tr(language, "generic.tips.noinput"));

                                        return {};
                                    });
                            }

                            this->getDatabase()->set_ptr("/" + id + "/title/" + mObjectTitle, mObjectData);

                            return this->getDatabase()->save();
                        });
                });
            });
    }

    ll::Expected<void> CdkPlugin::create(const std::string& id, int time, bool personal) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(CdkPluginErrorCode::Invalid));

        if (this->getDatabase()->has(id))
            return ll::makeErrorCodeError(makeErrorCode(CdkPluginErrorCode::Exists));

        nlohmann::ordered_json data = {
            { "personal", personal },
            { "player", nlohmann::ordered_json::array() },
            { "scores", nlohmann::ordered_json::object() },
            { "item", nlohmann::ordered_json::array() },
            { "title", nlohmann::ordered_json::object() },
            { "time", SystemUtils::toTimeCalculate(SystemUtils::getNowTime(), time * 60, "0") }
        };

        this->getDatabase()->set(id, data);
        
        return this->getDatabase()->save();
    }

    ll::Expected<void> CdkPlugin::remove(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(CdkPluginErrorCode::Invalid));

        this->getDatabase()->remove(id);

        return this->getDatabase()->save();
    }

    ll::Expected<void> CdkPlugin::convert(Player& player, const std::string& id) {
        if (!this->isValid()) 
            return ll::makeErrorCodeError(makeErrorCode(CdkPluginErrorCode::Invalid));

        return LanguagePlugin::getShared()->getLanguage(player)
            .and_then([this, id, &player](const std::string& language) -> ll::Expected<void> {
                return this->getDatabase()->get<nlohmann::ordered_json>(id)
                    .and_then([this, id, language, &player](nlohmann::ordered_json data) -> ll::Expected<void> {
                        if (data.is_null()) {
                            player.sendMessage(tr(language, "cdk.convert.tips1"));

                            return ll::makeErrorCodeError(makeErrorCode(CdkPluginErrorCode::NotFound));
                        }

                        if (SystemUtils::isPastOrPresent(data.value("time", ""))) {
                            player.sendMessage(tr(language, "cdk.convert.tips1"));

                            this->getDatabase()->remove(id);

                            return this->getDatabase()->save();
                        }

                        std::string mUuid = player.getUuid().asString();
                        if (auto it = data.value<nlohmann::ordered_json>("player", {}); std::find(it.begin(), it.end(), mUuid) != it.end()) {
                            player.sendMessage(tr(language, "cdk.convert.tips2"));

                            return ll::makeErrorCodeError(makeErrorCode(CdkPluginErrorCode::Received));
                        }

                        for (auto elements = data.value<nlohmann::ordered_json>("title", {}); auto& element : elements.items()) {
                            auto result = ChatPlugin::getShared()->addTitle(player, element.key(), element.value());
                            if (!result.has_value()) {
                                if (!result.error().isA<ll::ErrorCodeError>() || result.error().as<ll::ErrorCodeError>().ec != ChatPlugin::makeErrorCode(ChatPluginErrorCode::Invalid))
                                    return ll::Unexpected(result.error());
                            }
                        }

                        for (auto elements = data.value<nlohmann::ordered_json>("scores", {}); auto& element : elements.items())
                            ScoreboardUtils::addScore(player, element.key(), element.value());

                        for (auto& value : data.value<nlohmann::ordered_json>("item", {})) {
                            if (value.value("type", "") == "nbt") {
                                ItemStack itemStack = ItemStack::fromTag(CompoundTag::fromSnbt(value.value("id", ""))->mTags);
                                InventoryUtils::giveItem(player, itemStack, static_cast<int>(itemStack.mCount));
                            } else {
                                Bedrock::Safety::RedactableString mRedactableString;
                                mRedactableString.mUnredactedString = value.value("name", "");
                                
                                auto itemStack = std::make_unique<ItemStack>();
                                itemStack->reinit(value.value("id", ""), 1, value.value("specialvalue", 0));
                                itemStack->setCustomName(mRedactableString);
                                
                                InventoryUtils::giveItem(player, *itemStack, value.value("quantity", 1));
                            }
                        }

                        player.refreshInventory();
                        player.sendMessage(tr(language, "cdk.convert.tips3"));

                        if (data.value("personal", false))
                            this->getDatabase()->remove(id);
                        else {
                            data.at("player").push_back(mUuid);

                            this->getDatabase()->set(id, data);
                        }

                        return this->getDatabase()->save()
                            .transform([this, id, &player]() -> void {
                                this->getLogger()->info(fmt::runtime(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "cdk.log3"), player)), id);
                            });
                    });
            });
    }

    ll::Expected<std::vector<std::string>> CdkPlugin::getCdks() {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(CdkPluginErrorCode::Invalid));

        return this->getDatabase()->keys();
    }

    ll::Expected<bool> CdkPlugin::has(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(CdkPluginErrorCode::Invalid));

        return this->getDatabase()->has(id);
    }

    bool CdkPlugin::isValid() {
        return this->getLogger() != nullptr && this->getDatabase() != nullptr;
    }

    std::string CdkPlugin::getName() {
        return "CdkPlugin";
    }

    modules::ModulePriority CdkPlugin::getPriority() {
        return modules::ModulePriority::High;
    }

    ll::Expected<bool> CdkPlugin::load() {
        if (!ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Cdk)
            return false;

        auto mDataPath = std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("ConfigPath")->data());

        this->mImpl->db = std::make_shared<JsonStorage>(mDataPath / "cdk.json");
        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        this->mImpl->ModuleEnabled = true;
        this->mImpl->mGuiPath = (std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("GuiPath")->data()) / "cdk.lcui").string();

        return this->mImpl->db->load()
            .transform([]() -> bool {
                return true;
            });
    }

    ll::Expected<bool> CdkPlugin::unload() {
        if (!this->mImpl->ModuleEnabled)
            return false;

        this->mImpl->db.reset();
        this->mImpl->logger.reset();
        this->mImpl->ModuleEnabled = false;

        return true;
    }

    ll::Expected<bool> CdkPlugin::registry() {
        if (!this->mImpl->ModuleEnabled)
            return false;

        return this->registeryUI()
            .transform([this]() -> bool {
                this->registeryCommand();

                this->mImpl->mRegistered.store(true, std::memory_order_release);

                return true;
            });
    }

    ll::Expected<bool> CdkPlugin::unregistry() {
        if (!this->mImpl->ModuleEnabled)
            return false;

        return this->getDatabase()->save()
            .transform([this]() -> bool {
                this->mImpl->mRegistered.store(false, std::memory_order_release);

                return true;
            });
    }
}
