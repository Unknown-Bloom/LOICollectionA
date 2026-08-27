#include <gtest/gtest.h>

#include <string>

#include "LOICollectionA/frontend/sandbox/ScriptPermission.h"

using namespace LOICollection::frontend::sandbox;

namespace {
    const char* kWalletPermission = R"json({
        "defaultPolicy": "deny",
        "scripts": {
            "wallet.lcui": {
                "enabled": true,
                "commands": { "allow": false, "templates": [] },
                "gui": {
                    "values": ["wallet.players.online", "wallet.rank"],
                    "requests": ["wallet.info", "wallet.transfer.submit"],
                    "callbacks": ["wallet.wealth", "wallet.transfer.confirm"]
                }
            },
            "menu.lcui": {
                "enabled": true,
                "commands": { "allow": true, "templates": ["give ${player} minecraft:diamond 1"] },
                "gui": { "values": [], "requests": [], "callbacks": [] }
            },
            "open.lcui": {
                "enabled": true,
                "commands": { "allow": true, "templates": [] },
                "gui": { "values": [], "requests": [], "callbacks": [] }
            },
            "disabled.lcui": {
                "enabled": false,
                "commands": { "allow": true, "templates": [] },
                "gui": { "values": ["anything"], "requests": [], "callbacks": [] }
            }
        }
    })json";
}

TEST(ScriptPermissionTest, ParsesFromJson) {
    std::string error;
    auto gate = PermissionGate::fromJson(kWalletPermission, error);
    ASSERT_TRUE(gate.has_value());
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(gate->isScriptEnabled("wallet.lcui"));
    EXPECT_FALSE(gate->isScriptEnabled("unknown.lcui"));
    EXPECT_FALSE(gate->isScriptEnabled("disabled.lcui"));
}

TEST(ScriptPermissionTest, CommandDeniedWhenAllowFalse) {
    std::string error;
    auto gate = PermissionGate::fromJson(kWalletPermission, error);
    ASSERT_TRUE(gate.has_value());
    EXPECT_FALSE(gate->isCommandAllowed("wallet.lcui", "say hello", "Steve"));
}

TEST(ScriptPermissionTest, CommandTemplateMatchesAfterPlayerSubstitution) {
    std::string error;
    auto gate = PermissionGate::fromJson(kWalletPermission, error);
    ASSERT_TRUE(gate.has_value());

    EXPECT_TRUE(gate->isCommandAllowed("menu.lcui", "give ${player} minecraft:diamond 1", "Steve"));
    EXPECT_FALSE(gate->isCommandAllowed("menu.lcui", "give Steve minecraft:diamond 64", "Steve"));
    EXPECT_FALSE(gate->isCommandAllowed("menu.lcui", "op Steve", "Steve"));
}

TEST(ScriptPermissionTest, EmptyTemplatesAllowAnyCommand) {
    std::string error;
    auto gate = PermissionGate::fromJson(kWalletPermission, error);
    ASSERT_TRUE(gate.has_value());
    EXPECT_TRUE(gate->isCommandAllowed("open.lcui", "say anything", "Steve"));
}

TEST(ScriptPermissionTest, GuiIdWhitelist) {
    std::string error;
    auto gate = PermissionGate::fromJson(kWalletPermission, error);
    ASSERT_TRUE(gate.has_value());

    EXPECT_TRUE(gate->isGuiValueAllowed("wallet.lcui", "wallet.rank"));
    EXPECT_FALSE(gate->isGuiValueAllowed("wallet.lcui", "wallet.history"));

    EXPECT_TRUE(gate->isGuiRequestAllowed("wallet.lcui", "wallet.transfer.submit"));
    EXPECT_FALSE(gate->isGuiRequestAllowed("wallet.lcui", "wallet.redenvelope.submit"));

    EXPECT_TRUE(gate->isGuiCallbackAllowed("wallet.lcui", "wallet.wealth"));
    EXPECT_FALSE(gate->isGuiCallbackAllowed("wallet.lcui", "blacklist.add"));
}

TEST(ScriptPermissionTest, NavigationIsAllowedForEnabledScripts) {
    std::string error;
    auto gate = PermissionGate::fromJson(kWalletPermission, error);
    ASSERT_TRUE(gate.has_value());

    EXPECT_TRUE(gate->isGuiNavigationAllowed("wallet.lcui"));
    EXPECT_FALSE(gate->isGuiNavigationAllowed("disabled.lcui"));
}
