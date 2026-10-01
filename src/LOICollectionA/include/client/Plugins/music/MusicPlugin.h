#pragma once

#include <memory>
#include <string>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/include/client/Plugins/music/NowPlaying.h"

#include "LOICollectionA/include/ModuleBase.h"
#include "LOICollectionA/include/ModManager.h"

namespace ll::io {
    class Logger;
}

namespace LOICollection::client::Plugins::music {
    enum class MusicPluginErrorCode : int {
        Invalid    = 1,
        PollFailed = 2
    };

    struct MusicPluginErrorCategory : std::error_category {
        [[nodiscard]] const char* name() const noexcept override {
            return "MusicPluginError";
        }

        [[nodiscard]] std::string message(int ev) const override {
            switch (static_cast<MusicPluginErrorCode>(ev)) {
                case MusicPluginErrorCode::Invalid:    return "Plugin is invalid";
                case MusicPluginErrorCode::PollFailed: return "Failed to poll media session";
                default:                               return "Unknown";
            }
        }
    };

    class MusicPlugin : public std::enable_shared_from_this<MusicPlugin>,
                        public modules::ModuleBase,
                        public modules::AutoRegister<MusicPlugin> {
    public:
        ~MusicPlugin();

        MusicPlugin(MusicPlugin const&)            = delete;
        MusicPlugin(MusicPlugin&&)                 = delete;
        MusicPlugin& operator=(MusicPlugin const&) = delete;
        MusicPlugin& operator=(MusicPlugin&&)      = delete;

    public:
        LOICOLLECTION_A_NDAPI static std::shared_ptr<MusicPlugin> getShared();
        LOICOLLECTION_A_NDAPI static std::error_code makeErrorCode(MusicPluginErrorCode e);

        LOICOLLECTION_A_NDAPI std::shared_ptr<ll::io::Logger> getLogger();

        LOICOLLECTION_A_API NowPlayingTrack getCurrentTrack() const;

        LOICOLLECTION_A_API void replay();

        LOICOLLECTION_A_NDAPI bool isValid();

    public:
        LOICOLLECTION_A_NDAPI std::string getName() override;

        LOICOLLECTION_A_NDAPI modules::ModulePriority getPriority() override;

        LOICOLLECTION_A_API   ll::Expected<bool> load() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unload() override;
        LOICOLLECTION_A_API   ll::Expected<bool> registry() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unregistry() override;

    private:
        MusicPlugin();

        void startPoll();
        void stopPoll();
        void pollLoop();

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
