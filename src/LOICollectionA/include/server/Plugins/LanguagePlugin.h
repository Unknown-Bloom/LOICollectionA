#pragma once

#include <memory>
#include <string>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/include/ModuleBase.h"
#include "LOICollectionA/include/ModManager.h"

class Player;
class BlockRepository;

namespace ll::io {
    class Logger;
}

namespace LOICollection::server::Plugins {
    class LanguagePlugin : public std::enable_shared_from_this<LanguagePlugin>,
                           public modules::ModuleBase,
                           public modules::AutoRegister<LanguagePlugin> {
    public:
        ~LanguagePlugin();

        LanguagePlugin(LanguagePlugin const&) = delete;
        LanguagePlugin(LanguagePlugin&&) = delete;
        LanguagePlugin& operator=(LanguagePlugin const&) = delete;
        LanguagePlugin& operator=(LanguagePlugin&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI static std::shared_ptr<LanguagePlugin> getShared();

        LOICOLLECTION_A_NDAPI std::shared_ptr<ll::io::Logger> getLogger();

        LOICOLLECTION_A_NDAPI ll::Expected<std::string> getLanguage(const std::string& uuid);
        LOICOLLECTION_A_NDAPI ll::Expected<std::string> getLanguage(Player& player);

        LOICOLLECTION_A_NDAPI ll::Expected<void> set(Player& player, const std::string& langcode);

    public:
        LOICOLLECTION_A_NDAPI std::string getName() override;

        LOICOLLECTION_A_NDAPI modules::ModulePriority getPriority() override;

        LOICOLLECTION_A_API   ll::Expected<bool> load() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unload() override;
        LOICOLLECTION_A_API   ll::Expected<bool> registry() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unregistry() override;

    private:
        LanguagePlugin();

        ll::Expected<void> registeryUI();

        void registeryCommand();
        void listenEvent();
        void unlistenEvent();

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}