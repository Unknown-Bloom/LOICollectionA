#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

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
    enum class ChatPluginErrorCode : int {
        Invalid = 1,
        TitleNotFound = 2,
        BlacklistNotFound = 3
    };

    struct ChatPluginErrorCategory : std::error_category {
        [[nodiscard]] const char* name() const noexcept override {
            return "ChatPluginError";
        }

        [[nodiscard]] std::string message(int ev) const override {
            switch (static_cast<ChatPluginErrorCode>(ev)) {
                case ChatPluginErrorCode::Invalid: return "Plugin is invalid";
                case ChatPluginErrorCode::TitleNotFound: return "Chat title data not found";
                case ChatPluginErrorCode::BlacklistNotFound: return "Chat blacklist data not found";
                default:
                    return "Unknown";
            }
        }
    };

    class ChatPlugin : public std::enable_shared_from_this<ChatPlugin>,
                       public modules::ModuleBase,
                       public modules::AutoRegister<ChatPlugin> {
    public:
        ~ChatPlugin();

        ChatPlugin(ChatPlugin const&) = delete;
        ChatPlugin(ChatPlugin&&) = delete;
        ChatPlugin& operator=(ChatPlugin const&) = delete;
        ChatPlugin& operator=(ChatPlugin&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI static std::shared_ptr<ChatPlugin> getShared();
        LOICOLLECTION_A_NDAPI static std::error_code makeErrorCode(ChatPluginErrorCode e);

        LOICOLLECTION_A_NDAPI std::shared_ptr<BlockRepository> getDatabase();
        LOICOLLECTION_A_NDAPI std::shared_ptr<ll::io::Logger> getLogger();

        LOICOLLECTION_A_NDAPI ll::Expected<void> setTitle(Player& player, const std::string& text);

        LOICOLLECTION_A_NDAPI ll::Expected<void> addTitle(Player& player, const std::string& text, int time);
        LOICOLLECTION_A_NDAPI ll::Expected<void> addBlacklist(Player& player, Player& target);
        LOICOLLECTION_A_NDAPI ll::Expected<void> delTitle(Player& player, const std::string& text);
        LOICOLLECTION_A_NDAPI ll::Expected<void> delBlacklist(Player& player, const std::string& id);

        LOICOLLECTION_A_NDAPI ll::Expected<std::string> getTitle(Player& player);
        LOICOLLECTION_A_NDAPI ll::Expected<std::string> getTitleTime(Player& player, const std::string& text);
        LOICOLLECTION_A_NDAPI ll::Expected<std::string> getBlacklist(Player& player, Player& target);

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getTitles(Player& player);
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getBlacklist(Player& player);

        LOICOLLECTION_A_NDAPI ll::Expected<std::unordered_map<std::string, std::string>> getBlacklistData(const std::string& id);
        
        LOICOLLECTION_A_NDAPI ll::Expected<bool> hasTitle(Player& player, const std::string& text);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> hasBlacklist(Player& player, const std::string& id);
        
        LOICOLLECTION_A_NDAPI bool isValid();

    public:
        LOICOLLECTION_A_NDAPI int getBlacklistUpload();

    public:
        LOICOLLECTION_A_NDAPI std::string getName() override;

        LOICOLLECTION_A_NDAPI modules::ModulePriority getPriority() override;

        LOICOLLECTION_A_API   ll::Expected<bool> load() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unload() override;
        LOICOLLECTION_A_API   ll::Expected<bool> registry() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unregistry() override;

    private:
        ChatPlugin();

        ll::Expected<void> registeryUI();

        void registeryCommand();
        void listenEvent();
        void unlistenEvent();

        struct operation;

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
