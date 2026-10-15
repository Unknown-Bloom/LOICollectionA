#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "LOICollectionA/LOICollectionA.h"
#include "LOICollectionA/base/ServiceProvider.h"
#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/frontend/sandbox/ScriptPermission.h"

using namespace LOICollection::frontend::sandbox;

namespace {
    constexpr const char* kCapabilityPermission = R"json({
        "defaultPolicy": "deny",
        "defaultTrust": "user",
        "trustLevels": {
            "builtin": ["GuiRead", "GuiWrite", "ServerCommand", "PlayerDataRead", "PlayerDataWrite", "StorageRead", "StorageWrite"],
            "admin":   ["GuiRead", "GuiWrite", "ServerCommand", "PlayerDataRead", "PlayerDataWrite", "StorageRead", "StorageWrite"],
            "user":    ["GuiRead", "GuiWrite"],
            "noperm":  []
        },
        "scripts": {
            "plain": {
                "enabled": true,
                "commands": { "allow": true, "templates": ["do plain"] },
                "gui": { "values": ["v"], "requests": [], "callbacks": [], "navigations": [] }
            },
            "priv": {
                "enabled": true,
                "trust": "builtin",
                "commands": { "allow": true, "templates": ["do priv"] },
                "gui": { "values": [], "requests": [], "callbacks": [], "navigations": [] }
            },
            "restricted": {
                "enabled": true,
                "trust": "noperm",
                "gui": { "values": ["v"], "requests": [], "callbacks": [], "navigations": [] }
            }
        }
    })json";
}

TEST(ScriptCapabilityTest, DefaultTrustResolvesToUserCapabilities) {
    std::string error;
    auto gate = PermissionGate::fromJson(kCapabilityPermission, error);
    ASSERT_TRUE(gate.has_value()) << error;

    EXPECT_TRUE(gate->hasCapability("plain", Capability::GuiRead));
    EXPECT_TRUE(gate->hasCapability("plain", Capability::GuiWrite));
    EXPECT_FALSE(gate->hasCapability("plain", Capability::ServerCommand));
    EXPECT_FALSE(gate->hasCapability("plain", Capability::PlayerDataRead));
}

TEST(ScriptCapabilityTest, ExplicitTrustOverridesDefault) {
    std::string error;
    auto gate = PermissionGate::fromJson(kCapabilityPermission, error);
    ASSERT_TRUE(gate.has_value()) << error;

    EXPECT_TRUE(gate->hasCapability("priv", Capability::ServerCommand));
    EXPECT_TRUE(gate->hasCapability("priv", Capability::PlayerDataWrite));
    EXPECT_TRUE(gate->hasCapability("priv", Capability::StorageRead));
}

TEST(ScriptCapabilityTest, UnknownTrustGrantsNothing) {
    std::string error;
    auto gate = PermissionGate::fromJson(kCapabilityPermission, error);
    ASSERT_TRUE(gate.has_value()) << error;

    EXPECT_FALSE(gate->hasCapability("restricted", Capability::GuiRead));
    EXPECT_FALSE(gate->hasCapability("restricted", Capability::GuiWrite));
}

TEST(ScriptCapabilityTest, CoarseGateBlocksCommandsForUserTrust) {
    std::string error;
    auto gate = PermissionGate::fromJson(kCapabilityPermission, error);
    ASSERT_TRUE(gate.has_value()) << error;

    EXPECT_FALSE(gate->isCommandAllowed("plain", "do plain"));
}

TEST(ScriptCapabilityTest, CoarseGateAllowsCommandsForBuiltinTrust) {
    std::string error;
    auto gate = PermissionGate::fromJson(kCapabilityPermission, error);
    ASSERT_TRUE(gate.has_value()) << error;

    EXPECT_TRUE(gate->isCommandAllowed("priv", "do priv"));
}

TEST(ScriptCapabilityTest, CoarseGateBlocksGuiWhenTrustLacksCapability) {
    std::string error;
    auto gate = PermissionGate::fromJson(kCapabilityPermission, error);
    ASSERT_TRUE(gate.has_value()) << error;

    EXPECT_FALSE(gate->isGuiValueAllowed("restricted", "v"));
}

TEST(ScriptCapabilityTest, CoarseAndFineGatesBothPassForUserGui) {
    std::string error;
    auto gate = PermissionGate::fromJson(kCapabilityPermission, error);
    ASSERT_TRUE(gate.has_value()) << error;

    EXPECT_TRUE(gate->isGuiValueAllowed("plain", "v"));
    EXPECT_FALSE(gate->isGuiValueAllowed("plain", "other"));
}

TEST(ScriptCapabilityTest, UnknownCapabilityNamesAreIgnored) {
    constexpr const char* json = R"json({
        "defaultPolicy": "deny",
        "defaultTrust": "odd",
        "trustLevels": {
            "odd": ["GuiRead", "NonexistentCapability"]
        },
        "scripts": {
            "s": { "enabled": true, "gui": { "values": ["v"], "requests": [], "callbacks": [], "navigations": [] } }
        }
    })json";

    std::string error;
    auto gate = PermissionGate::fromJson(json, error);
    ASSERT_TRUE(gate.has_value()) << error;

    EXPECT_TRUE(gate->hasCapability("s", Capability::GuiRead));
}
