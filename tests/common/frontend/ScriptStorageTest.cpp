#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/frontend/storage/ScriptStorage.h"

#include "LOICollectionA/frontend/sandbox/ScriptPermission.h"

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

namespace {
    const char* kStoragePermission = R"json({
        "defaultPolicy": "deny",
        "defaultTrust": "reader",
        "trustLevels": {
            "reader": ["StorageRead", "PlayerDataRead"],
            "writer": ["StorageRead", "StorageWrite", "PlayerDataRead", "PlayerDataWrite"]
        },
        "scripts": {
            "writer": { "enabled": true, "trust": "writer" }
        }
    })json";

    struct StorageFixture : testing::Test {
        std::filesystem::path root;

        void SetUp() override {
            root = std::filesystem::temp_directory_path() / "loicc-script-storage-test";
            std::filesystem::remove_all(root);

            std::string error;
            auto gate = sandbox::PermissionGate::fromJson(kStoragePermission, error);
            EXPECT_TRUE(gate.has_value()) << error;

            auto permission = std::make_shared<sandbox::ScriptPermissionService>();
            permission->setGate(std::move(*gate));

            ServiceProvider::getInstance().registerInstance(permission);
            ServiceProvider::getInstance().registerInstance(std::make_shared<storage::ScriptStorage>(root));
        }

        void TearDown() override {
            std::filesystem::remove_all(root);
        }

        static std::string evalAs(const std::string& code, const std::string& scriptId, const std::string& playerId = "") {
            Context ctx;
            ctx.scriptId = scriptId;

            if (!playerId.empty())
                ctx.params[Context::kOwnerIdKey] = playerId;

            return eval(code, ctx);
        }
    };
}

TEST_F(StorageFixture, ServiceRoundTripsJsonValues) {
    auto service = std::make_shared<storage::ScriptStorage>(root / "direct");

    ASSERT_TRUE(service->set("demo", "int", 7).has_value());
    ASSERT_TRUE(service->set("demo", "float", 2.5f).has_value());
    ASSERT_TRUE(service->set("demo", "text", std::string("中文")).has_value());
    ASSERT_TRUE(service->set("demo", "flag", true).has_value());

    EXPECT_EQ(std::get<int>(*service->get("demo", "int")), 7);
    EXPECT_EQ(std::get<float>(*service->get("demo", "float")), 2.5f);
    EXPECT_EQ(std::get<std::string>(*service->get("demo", "text")), "中文");
    EXPECT_EQ(std::get<bool>(*service->get("demo", "flag")), true);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(*service->get("demo", "missing")));
}

TEST_F(StorageFixture, ServiceRoundTripsArraysAndObjects) {
    auto service = std::make_shared<storage::ScriptStorage>(root / "nested");

    auto array = std::make_shared<ArrayValue>();
    array->elements.emplace_back(1);
    auto inner = std::make_shared<ArrayValue>();
    inner->elements.emplace_back(std::string("a"));
    array->elements.emplace_back(inner);

    ASSERT_TRUE(service->set("demo", "array", array).has_value());

    auto stored = std::get<ArrayRef>(*service->get("demo", "array"));
    ASSERT_EQ(stored->elements.size(), 2u);
    EXPECT_EQ(std::get<int>(stored->elements[0]), 1);
    EXPECT_EQ(std::get<std::string>(std::get<ArrayRef>(stored->elements[1])->elements[0]), "a");

    auto object = std::make_shared<Object>();
    object->className = "Object";
    object->classIndex = -1;
    object->assign("name", std::string("Qoder"));
    object->assign("count", 3);

    ASSERT_TRUE(service->set("demo", "object", object).has_value());

    auto loaded = std::get<ObjectRef>(*service->get("demo", "object"));
    ASSERT_TRUE(loaded);
    EXPECT_EQ(std::get<std::string>(*loaded->fieldAt(loaded->slotOf("name"), "name")), "Qoder");
    EXPECT_EQ(std::get<int>(*loaded->fieldAt(loaded->slotOf("count"), "count")), 3);
}

TEST_F(StorageFixture, ServiceTracksKeysAndPersistence) {
    {
        storage::ScriptStorage service(root / "persist");
        ASSERT_TRUE(service.set("demo", "b", 2).has_value());
        ASSERT_TRUE(service.set("demo", "a", 1).has_value());
        EXPECT_EQ(service.keys("demo").value()->elements.size(), 2u);
    }

    storage::ScriptStorage service(root / "persist");
    EXPECT_TRUE(service.has("demo", "a").value());
    EXPECT_EQ(std::get<int>(*service.get("demo", "a")), 1);
    EXPECT_TRUE(service.remove("demo", "a").value());
    EXPECT_FALSE(service.has("demo", "a").value());
    EXPECT_FALSE(service.remove("demo", "a").value());

    ASSERT_TRUE(service.clear("demo").has_value());
    EXPECT_TRUE(service.keys("demo").value()->elements.empty());
}

TEST_F(StorageFixture, ServiceIsolatesScriptsAndPlayers) {
    storage::ScriptStorage service(root / "isolation");

    ASSERT_TRUE(service.set("alpha", "score", 1).has_value());
    ASSERT_TRUE(service.set("beta", "score", 2).has_value());
    EXPECT_EQ(std::get<int>(*service.get("alpha", "score")), 1);
    EXPECT_EQ(std::get<int>(*service.get("beta", "score")), 2);

    ASSERT_TRUE(service.playerSet("player-1", "alpha", "score", 10).has_value());
    ASSERT_TRUE(service.playerSet("player-2", "alpha", "score", 20).has_value());
    EXPECT_EQ(std::get<int>(*service.playerGet("player-1", "alpha", "score")), 10);
    EXPECT_EQ(std::get<int>(*service.playerGet("player-2", "alpha", "score")), 20);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(*service.playerGet("player-1", "beta", "score")));
}

TEST_F(StorageFixture, BuiltinsStoreAndReadValues) {
    EXPECT_EQ(evalAs("storage::set(\"answer\", 42)", "writer"), "42");
    EXPECT_EQ(evalAs("storage::get(\"answer\")", "writer"), "42");
    EXPECT_EQ(evalAs("storage::has(\"answer\")", "writer"), "true");
    EXPECT_EQ(evalAs("storage::keys()", "writer"), "[answer]");
    EXPECT_EQ(evalAs("storage::get(\"missing\")", "writer"), "None");
    EXPECT_EQ(evalAs("storage::remove(\"answer\")", "writer"), "true");
    EXPECT_EQ(evalAs("storage::remove(\"answer\")", "writer"), "false");
}

TEST_F(StorageFixture, BuiltinsEnforceReadAndWriteCapabilities) {
    EXPECT_EQ(evalAs("storage::get(\"missing\")", "reader"), "None");
    EXPECT_THROW(evalAs("storage::set(\"key\", 1)", "reader"), std::runtime_error);
    EXPECT_THROW(evalAs("storage::clear()", "reader"), std::runtime_error);
}

TEST_F(StorageFixture, PlayerDataUsesOwnerContext) {
    EXPECT_EQ(evalAs("playerdata::set(\"coins\", 12)", "writer", "player-alice"), "12");
    EXPECT_EQ(evalAs("playerdata::get(\"coins\")", "writer", "player-alice"), "12");
    EXPECT_EQ(evalAs("playerdata::get(\"coins\")", "writer", "player-bob"), "None");
    EXPECT_THROW(evalAs("playerdata::get(\"coins\")", "writer"), std::runtime_error);
}

TEST_F(StorageFixture, PermissionDenialMentionsCapability) {
    try {
        evalAs("storage::set(\"key\", 1)", "reader");
        FAIL() << "expected permission error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("StorageWrite"), std::string::npos);
    }
}
