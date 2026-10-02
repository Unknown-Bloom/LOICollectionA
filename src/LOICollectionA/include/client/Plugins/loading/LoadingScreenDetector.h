#pragma once

#include <memory>
#include <string>
#include <vector>

#include "LOICollectionA/base/Macro.h"

namespace ll::io {
    class Logger;
}

namespace OreUI {
    class Scene;
}

class ProgressScreenController;

namespace Json {
    class Value;
}

namespace LOICollection::client::Plugins::loading {
    class LoadingScreenDetector {
    public:
        ~LoadingScreenDetector();

        LoadingScreenDetector(LoadingScreenDetector const&)            = delete;
        LoadingScreenDetector(LoadingScreenDetector&&)                 = delete;
        LoadingScreenDetector& operator=(LoadingScreenDetector const&) = delete;
        LoadingScreenDetector& operator=(LoadingScreenDetector&&)      = delete;

    public:
        LOICOLLECTION_A_NDAPI static LoadingScreenDetector& getInstance();

        LOICOLLECTION_A_API void setLogger(std::shared_ptr<ll::io::Logger> logger);
        LOICOLLECTION_A_API void setRouteKeywords(std::vector<std::string> keywords);

        LOICOLLECTION_A_NDAPI bool install();

        LOICOLLECTION_A_API void uninstall();

        LOICOLLECTION_A_NDAPI bool isLoadingScreenActive() const;
        LOICOLLECTION_A_NDAPI bool isSceneHookActive() const;

        LOICOLLECTION_A_NDAPI float loadingProgress() const;

        LOICOLLECTION_A_NDAPI bool isProgressHookActive() const;

        LOICOLLECTION_A_NDAPI std::string progressMessage() const;

        LOICOLLECTION_A_NDAPI bool isBlockingModalActive() const;

        LOICOLLECTION_A_NDAPI std::string currentRoute() const;

        LOICOLLECTION_A_API void onSceneRendered(::OreUI::Scene& scene);
        LOICOLLECTION_A_API void onProgressScreenTicked(::ProgressScreenController& controller);
        LOICOLLECTION_A_API void onProgressScreenVars(::ProgressScreenController& controller, ::Json::Value& globalVars);

    private:
        LoadingScreenDetector();

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
