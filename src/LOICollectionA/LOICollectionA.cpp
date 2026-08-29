#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <ll/api/Config.h>
#include <ll/api/io/Logger.h>
#include <ll/api/Mod/NativeMod.h>
#include <ll/api/Mod/RegisterHelper.h>

#include <mc/locale/I18n.h>
#include <mc/locale/Localization.h>

#include "LOICollectionA/utils/I18nUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/include/ModuleBase.h"
#include "LOICollectionA/include/ModManager.h"
#include "LOICollectionA/include/CallbackUtils.h"
#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/frontend/sandbox/ScriptPermission.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/LOICollectionA.h"

namespace LOICollection {
    A& A::getInstance() {
        static A instance;
        return instance;
    }

    bool A::load() {
        ll::io::Logger& logger = this->mSelf.getLogger();
        const std::filesystem::path& dataFilePath = this->mSelf.getDataDir();
        const std::filesystem::path& langFilePath = this->mSelf.getLangDir();
        const std::filesystem::path& guiFilePath = this->mSelf.getModDir() / "gui";
        const std::filesystem::path& configDataPath = this->mSelf.getConfigDir();
        const std::filesystem::path& configFilePath = configDataPath / "config.json";

        Config::SynchronousPluginConfigVersion(this->config);
        logger.info("Loading LOICollection - A (Version {})", Config::GetVersion());
        logger.info("Protocol - Mojang Eula (https://account.mojang.com/documents/minecraft_eula)");
        
        if (!std::filesystem::exists(configFilePath)) {
            logger.info("Plugin - Configurations not found.");
            logger.info("Plugin - Saving default configurations.");

            if (!ll::config::saveConfig(this->config, configFilePath)) {
                logger.error("Failed to save default configurations.");
                
                return false;
            }
        }

        Config::SynchronousPluginConfigType(this->config, configFilePath.string()).or_else([&logger](const ll::Error& err) -> ll::Expected<void> { 
            err.log(logger);
            return {};
        });

        if (!ll::config::loadConfig(this->config, configFilePath)) {
            logger.info("Plugin - Update configurations.");

            if (!ll::config::saveConfig(this->config, configFilePath)) {
                logger.error("Failed to save default configurations.");

                return false;
            }
        }
        
        ServiceProvider::getInstance().registerInstance<ReadOnlyWrapper<Config::C_Config>>(
            std::make_shared<ReadOnlyWrapper<Config::C_Config>>(this->config), "Config"
        );

        logger.info("Initialization of configurations completed.");

        std::filesystem::create_directory(dataFilePath);
        ServiceProvider::getInstance().registerInstance<std::string>(std::make_shared<std::string>(dataFilePath.string()), "DataPath");
        ServiceProvider::getInstance().registerInstance<std::string>(std::make_shared<std::string>(guiFilePath.string()), "GuiPath");
        ServiceProvider::getInstance().registerInstance<std::string>(std::make_shared<std::string>(configDataPath.string()), "ConfigPath");
        ServiceProvider::getInstance().registerInstance<SQLiteStorage>(std::make_shared<SQLiteStorage>((dataFilePath / "settings.db").string()), "SettingsDB");

        {
            auto permission = std::make_shared<frontend::sandbox::ScriptPermissionService>();
            const std::filesystem::path permissionPath = this->mSelf.getModDir() / this->config.ScriptPermission.PermissionFilePath;

            if (std::filesystem::exists(permissionPath)) {
                std::ifstream stream(permissionPath);
                const std::string json((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

                std::string error;
                if (auto gate = frontend::sandbox::PermissionGate::fromJson(json, error))
                    permission->setGate(std::move(*gate));
                else
                    logger.error("Failed to parse script permission: {}", error);
            }

            ServiceProvider::getInstance().registerInstance(permission);
        }

        std::vector<std::string> mMods = modules::ModManager::getInstance().mods();
        std::for_each(mMods.begin(), mMods.end(), [&logger](const std::string& mod) -> void {
            std::shared_ptr<modules::ModuleBase> mModule = modules::ModManager::getInstance().getModule(mod);

            if (!mModule) {
                logger.error("Failed to get mod {}", mod);
                return;
            }

            mModule->load().or_else([&logger](const ll::Error& err) -> ll::Expected<bool> { 
                err.log(logger);
                return false;
            });
        });

        logger.info("Initialization of database completed.");
        
        std::filesystem::create_directory(langFilePath);
        I18nUtils::getInstance() = std::make_unique<I18nUtils>(langFilePath.string());
        logger.info("Initialization of language completed.");

        logger.info("Initialization of plugins completed.");

        return true;
    }

    bool A::unload() {
        ll::io::Logger& logger = this->mSelf.getLogger();

        std::vector<std::string> mMods = modules::ModManager::getInstance().mods();
        std::for_each(mMods.begin(), mMods.end(), [&logger](const std::string& mod) -> void {
            std::shared_ptr<modules::ModuleBase> mModule = modules::ModManager::getInstance().getModule(mod);

            if (!mModule) {
                logger.error("Failed to get mod mod {}", mod);
                return;
            }

            mModule->unload().or_else([&logger](const ll::Error& err) -> ll::Expected<bool> { 
                err.log(logger);
                return false;
            });
        });

        return true;
    }

    bool A::enable() {
        ll::io::Logger& logger = this->mSelf.getLogger();

        I18nUtils::getInstance()->defaultLocale = this->config.ConsoleLanguage == "system" ?
            getI18n().getCurrentLanguage()->mCode.get() : this->config.ConsoleLanguage;

        LOICollectionAPI::CallbackUtils::getInstance().compile();

        std::vector<std::string> mMods = modules::ModManager::getInstance().mods();
        std::for_each(mMods.begin(), mMods.end(), [&logger](const std::string& mod) -> void {
            std::shared_ptr<modules::ModuleBase> mModule = modules::ModManager::getInstance().getModule(mod);

            if (!mModule) {
                logger.error("Failed to get mod {}", mod);
                return;
            }

            mModule->registry().or_else([&logger](const ll::Error& err) -> ll::Expected<bool> { 
                err.log(logger);
                return false;
            });
        });

        return true;
    }

    bool A::disable() {
        ll::io::Logger& logger = this->mSelf.getLogger();

        form::GUIManager::getInstance().releaseAllUI();

        std::vector<std::string> mMods = modules::ModManager::getInstance().mods();
        std::for_each(mMods.begin(), mMods.end(), [&logger](const std::string& mod) -> void {
            std::shared_ptr<modules::ModuleBase> mModule = modules::ModManager::getInstance().getModule(mod);

            if (!mModule) {
                logger.error("Failed to get mod {}", mod);
                return;
            }

            mModule->unregistry().or_else([&logger](const ll::Error& err) -> ll::Expected<bool> { 
                err.log(logger);
                return false;
            });
        });

        return true;
    }
}

LL_REGISTER_MOD(LOICollection::A, LOICollection::A::getInstance());
