#include <string>

#include <ll/api/Expected.h>
#include <ll/api/data/Version.h>
#include <ll/api/mod/Manifest.h>
#include <ll/api/mod/NativeMod.h>
#include <ll/api/reflection/Serialization.h>

#include <nlohmann/json.hpp>

#include "LOICollectionA/data/json/JsonStorage.h"

#include "LOICollectionA/LOICollectionA.h"

#include "LOICollectionA/ConfigPlugin.h"

namespace Config {
    C_Config mConfig;

    std::string GetVersion() {
        auto mVersion = LOICollection::A::getInstance().getSelf().getManifest().version;
        return mVersion.has_value() ? mVersion->to_string() : "unknown";
    }

    void MergePatch(nlohmann::ordered_json& source, nlohmann::ordered_json& patch) {
        if (!source.is_object() || !patch.is_object())
            return;

        for (auto it = patch.begin(); it != patch.end(); ++it) {
            if (!source.contains(it.key())) {
                source[it.key()] = it.value();
                continue;
            }

            if (it.value().type() != source[it.key()].type()) {
                source[it.key()] = patch[it.key()];
                continue;
            }

            MergePatch(source[it.key()], it.value());
        }
    }

    void SynchronousPluginConfigVersion(C_Config& config) {
        const int mPrime = 31;
        const int mMOD = 100000000;

        size_t mHash = 0;

        auto mVersion = LOICollection::A::getInstance().getSelf().getManifest().version;
        if (!mVersion.has_value())
            return;

        mHash = std::hash<std::string>()(mVersion->to_string()) % mMOD;
        mHash = (mHash * mPrime + mVersion->major) % mMOD;
        mHash = (mHash * mPrime + mVersion->minor) % mMOD;
        mHash = (mHash * mPrime + mVersion->patch) % mMOD;

        config.version = static_cast<int>(mHash);
    }

    ll::Expected<void> SynchronousPluginConfigType(C_Config& config, const std::string& path) {
        JsonStorage mConfigObject(path);

        return mConfigObject.load()
            .and_then([&mConfigObject, &config, path]() -> ll::Expected<void> {
                nlohmann::ordered_json mConfigJson = mConfigObject.get();
                nlohmann::ordered_json mPatchJson = nlohmann::ordered_json::parse(
                    ll::reflection::serialize<nlohmann::ordered_json>(config)->dump()
                );
                MergePatch(mConfigJson, mPatchJson);
                
                mConfigObject.write(mConfigJson);
                
                return mConfigObject.save();
            });
    }
}
