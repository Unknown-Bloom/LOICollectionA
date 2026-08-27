#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

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
                "commands": { "allow": true, "templates": ["say hello"] },
                "gui": { "values": ["anything"], "requests": [], "callbacks": [] }
            }
        }
    })json";

    const std::vector<std::string> kProductionScriptIds = {
        "blacklist", "cdk", "chat", "language", "market", "market.auction",
        "market.quote", "market.store", "market.trade", "market.wanted",
        "menu", "mute", "notice", "pvp", "shop", "statistics", "tpa", "wallet",
    };

    std::filesystem::path locateProductionPermissionJson() {
#ifdef LOICOLLECTION_A_PERMISSION_JSON
        return LOICOLLECTION_A_PERMISSION_JSON;
#endif
        if (const char* env = std::getenv("LOICOLLECTION_A_PERMISSION_JSON"))
            return std::filesystem::path(env);

        std::filesystem::path current = std::filesystem::path(__FILE__).parent_path();
        while (!current.empty()) {
            std::filesystem::path candidate = current / "assets" / "common" / "gui" / "permission.json";
            if (std::filesystem::exists(candidate))
                return candidate;
            current = current.parent_path();
        }

        return {};
    }
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
    EXPECT_FALSE(gate->isCommandAllowed("wallet.lcui", "say hello"));
}

TEST(ScriptPermissionTest, CommandMatchesTemplate) {
    std::string error;
    auto gate = PermissionGate::fromJson(kWalletPermission, error);
    ASSERT_TRUE(gate.has_value());

    EXPECT_TRUE(gate->isCommandAllowed("menu.lcui", "give ${player} minecraft:diamond 1"));
    EXPECT_FALSE(gate->isCommandAllowed("menu.lcui", "give Steve minecraft:diamond 64"));
    EXPECT_FALSE(gate->isCommandAllowed("menu.lcui", "op Steve"));
}

TEST(ScriptPermissionTest, EmptyTemplatesDenyCommands) {
    std::string error;
    auto gate = PermissionGate::fromJson(kWalletPermission, error);
    ASSERT_TRUE(gate.has_value());
    EXPECT_FALSE(gate->isCommandAllowed("open.lcui", "say anything"));
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

TEST(ScriptPermissionTest, ProductionScriptIdsAllEnabled) {
    const std::filesystem::path path = locateProductionPermissionJson();
    ASSERT_FALSE(path.empty());

    std::ifstream input(path);
    ASSERT_TRUE(input.is_open());

    std::stringstream buffer;
    buffer << input.rdbuf();

    const auto json = nlohmann::json::parse(buffer.str(), nullptr, false);
    ASSERT_FALSE(json.is_discarded());
    ASSERT_TRUE(json.contains("scripts"));
    EXPECT_EQ(json["scripts"].size(), kProductionScriptIds.size());

    std::string error;
    auto gate = PermissionGate::fromJson(buffer.str(), error);
    ASSERT_TRUE(gate.has_value()) << error;

    for (const std::string& id : kProductionScriptIds)
        EXPECT_TRUE(gate->isScriptEnabled(id)) << id;

    EXPECT_TRUE(gate->isCommandAllowed("menu", "say No permission"));
    EXPECT_TRUE(gate->isCommandAllowed("menu", "say No score"));
    EXPECT_TRUE(gate->isCommandAllowed("shop", "say No score"));
    EXPECT_FALSE(gate->isCommandAllowed("shop", "say Exit Shop"));
}
