#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <string_view>

#include "LOICollectionA/data/json/JsonStorage.h"

class JsonStorageTest : public testing::Test {
protected:
    std::filesystem::path tempDir;
    std::filesystem::path filePath;

    void SetUp() override {
        tempDir = std::filesystem::temp_directory_path() / "json_storage_test";

        std::filesystem::create_directories(tempDir);

        filePath = tempDir / "test.json";
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }

    void writeJsonFile(const std::string& content) {
        std::ofstream out(filePath);
        ASSERT_TRUE(out.is_open());
        out << content;
        out.close();
    }
};

TEST_F(JsonStorageTest, ConstructorCreatesFileWithEmptyJson) {
    ASSERT_FALSE(std::filesystem::exists(filePath));

    JsonStorage storage(filePath);
    EXPECT_TRUE(storage.load().has_value());

    EXPECT_TRUE(std::filesystem::exists(filePath));
    EXPECT_EQ(storage.get(), nlohmann::ordered_json::object());
}

TEST_F(JsonStorageTest, ConstructorLoadsExistingJson) {
    nlohmann::ordered_json expected = {{"name", "test"}, {"value", 42}};
    writeJsonFile(expected.dump());

    JsonStorage storage(filePath);
    EXPECT_TRUE(storage.load().has_value());

    EXPECT_EQ(storage.get(), expected);
}

TEST_F(JsonStorageTest, SetAndGetValue) {
    JsonStorage storage(filePath);
    EXPECT_TRUE(storage.load().has_value());

    storage.set("intKey", 100);

    auto intVal = storage.get<int>("intKey");
    EXPECT_TRUE(intVal.has_value());
    EXPECT_EQ(intVal.value(), 100);
    EXPECT_TRUE(storage.has("intKey"));

    storage.set("strKey", std::string("hello"));

    auto strVal = storage.get<std::string>("strKey");
    EXPECT_TRUE(strVal.has_value());
    EXPECT_EQ(strVal.value(), "hello");

    auto missing = storage.get<int>("missing");
    EXPECT_FALSE(missing.has_value());
    EXPECT_FALSE(storage.has("missing"));
}

TEST_F(JsonStorageTest, SetAndGetWithPointer) {
    JsonStorage storage(filePath);
    EXPECT_TRUE(storage.load().has_value());

    storage.set_ptr("/user/name", std::string("Alice"));
    storage.set_ptr("/user/age", 30);

    auto name = storage.get_ptr<std::string>("/user/name");
    EXPECT_TRUE(name.has_value());
    EXPECT_EQ(name.value(), "Alice");

    auto age = storage.get_ptr<int>("/user/age");
    EXPECT_TRUE(age.has_value());
    EXPECT_EQ(age.value(), 30);

    EXPECT_TRUE(storage.has_ptr("/user/name"));

    auto missing = storage.get_ptr<std::string>("/user/nick");
    EXPECT_EQ(missing.value_or("unknown"), "unknown");
}

TEST_F(JsonStorageTest, RemoveAndRemovePtr) {
    JsonStorage storage(filePath);
    storage.set("a", 1);
    storage.set_ptr("/x/y", 2);

    storage.remove("a");
    EXPECT_FALSE(storage.has("a"));

    storage.remove_ptr("/x/y");
    EXPECT_FALSE(storage.has_ptr("/x/y"));
}

TEST_F(JsonStorageTest, HasAndHasPtr) {
    JsonStorage storage(filePath);
    storage.set("visible", true);
    storage.set_ptr("/deep/hidden", 123);

    EXPECT_TRUE(storage.has("visible"));
    EXPECT_FALSE(storage.has("invisible"));
    EXPECT_TRUE(storage.has_ptr("/deep/hidden"));
    EXPECT_FALSE(storage.has_ptr("/deep/nowhere"));
}

TEST_F(JsonStorageTest, KeysReturnsTopLevelKeys) {
    JsonStorage storage(filePath);
    storage.set("first", 1);
    storage.set("second", 2);
    storage.set("third", 3);

    auto k = storage.keys();
    std::sort(k.begin(), k.end());

    std::vector<std::string> expected = {"first", "second", "third"};
    EXPECT_EQ(k, expected);
}

TEST_F(JsonStorageTest, WriteReplacesEntireJson) {
    JsonStorage storage(filePath);
    storage.set("old", "data");

    nlohmann::ordered_json newJson = {{"updated", true}, {"count", 10}};
    storage.write(newJson);

    EXPECT_EQ(storage.get(), newJson);
    EXPECT_FALSE(storage.has("old"));
}

TEST_F(JsonStorageTest, SavePersistsAndReloadsCorrectly) {
    nlohmann::ordered_json data = {{"persist", 123}, {"arr", {1, 2, 3}}};
    {
        JsonStorage storage(filePath);
        storage.write(data);
        EXPECT_TRUE(storage.save().has_value());
    }

    JsonStorage reloaded(filePath);
    EXPECT_TRUE(reloaded.load().has_value());
    EXPECT_EQ(reloaded.get(), data);
}

TEST_F(JsonStorageTest, ThreadSafetySetAndGet) {
    JsonStorage storage(filePath);
    EXPECT_TRUE(storage.load().has_value());

    const int numThreads = 10;
    const int iterations = 100;
    std::vector<std::thread> threads;

    threads.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&storage, i]() -> void {
            for (int j = 0; j < iterations; ++j) {
                storage.set("shared_key", i * 1000 + j);

                auto val = storage.get<int>("shared_key");
                if (val.has_value()) {
                    volatile int v = val.value();
                    (void)v;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    SUCCEED();
}
