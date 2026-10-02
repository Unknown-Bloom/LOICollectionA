#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <iterator>
#include <algorithm>

#include <ll/api/Expected.h>

#include "LOICollectionA/include/ModManager.h"
#include "LOICollectionA/include/ModuleBase.h"
#include "LOICollectionA/include/ModulePriority.h"

using namespace LOICollection::modules;

class ModManagerTestFakeModule : public ModuleBase {
public:
    ModManagerTestFakeModule(std::string name, ModulePriority priority) : mName(std::move(name)), mPriority(priority) {}

    std::string getName() override {
        return this->mName;
    }

    ModulePriority getPriority() override {
        return this->mPriority;
    }

    ll::Expected<bool> load() override {
        return true;
    }

    ll::Expected<bool> unload() override {
        return true;
    }

    ll::Expected<bool> registry() override {
        return true;
    }

    ll::Expected<bool> unregistry() override {
        return true;
    }

private:
    std::string mName;
    ModulePriority mPriority;
};

class ModManagerTest : public testing::Test {
protected:
    std::vector<std::string> mRegistered;

    void TearDown() override {
        for (const std::string& name : this->mRegistered)
            ModManager::getInstance().unregistry(name);

        this->mRegistered.clear();
    }

    std::shared_ptr<ModManagerTestFakeModule> Register(const std::string& name, ModulePriority priority) {
        auto module = std::make_shared<ModManagerTestFakeModule>(name, priority);

        ModManager::getInstance().registry(module, name, priority);

        this->mRegistered.push_back(name);

        return module;
    }

    std::ptrdiff_t IndexOf(const std::string& name) {
        std::vector<std::string> mods = ModManager::getInstance().mods();
        auto it = std::find(mods.begin(), mods.end(), name);

        return it == mods.end() ? -1 : std::distance(mods.begin(), it);
    }
};

TEST(ModManagerPriorityTest, ModulePriorityValues) {
    EXPECT_EQ(static_cast<int>(ModulePriority::Highest), 0);
    EXPECT_EQ(static_cast<int>(ModulePriority::High), 1);
    EXPECT_EQ(static_cast<int>(ModulePriority::Normal), 2);
    EXPECT_EQ(static_cast<int>(ModulePriority::Low), 3);
    EXPECT_EQ(static_cast<int>(ModulePriority::Lowest), 4);
}

TEST_F(ModManagerTest, GetModuleReturnsRegisteredInstanceAndNullForUnknownName) {
    auto highest = this->Register("ModManagerTestFakeHighest", ModulePriority::Highest);
    auto normal = this->Register("ModManagerTestFakeNormal", ModulePriority::Normal);
    auto low = this->Register("ModManagerTestFakeLow", ModulePriority::Low);

    EXPECT_EQ(ModManager::getInstance().getModule("ModManagerTestFakeHighest"), highest);
    EXPECT_EQ(ModManager::getInstance().getModule("ModManagerTestFakeNormal"), normal);
    EXPECT_EQ(ModManager::getInstance().getModule("ModManagerTestFakeLow"), low);
    EXPECT_EQ(ModManager::getInstance().getModule("ModManagerTestFakeUnknown"), nullptr);
    EXPECT_EQ(ModManager::getInstance().getModule(""), nullptr);
}

TEST_F(ModManagerTest, ModsOrderFollowsPriority) {
    this->Register("ModManagerTestFakeLow", ModulePriority::Low);
    this->Register("ModManagerTestFakeHighest", ModulePriority::Highest);
    this->Register("ModManagerTestFakeNormal", ModulePriority::Normal);

    std::ptrdiff_t highest = this->IndexOf("ModManagerTestFakeHighest");
    std::ptrdiff_t normal = this->IndexOf("ModManagerTestFakeNormal");
    std::ptrdiff_t low = this->IndexOf("ModManagerTestFakeLow");

    ASSERT_GE(highest, 0);
    ASSERT_GE(normal, 0);
    ASSERT_GE(low, 0);

    EXPECT_LT(highest, normal);
    EXPECT_LT(normal, low);
}

TEST_F(ModManagerTest, UnregistryRemovesExactlyOneModuleAndKeepsOrder) {
    auto highest = this->Register("ModManagerTestFakeHighest", ModulePriority::Highest);
    this->Register("ModManagerTestFakeNormal", ModulePriority::Normal);
    this->Register("ModManagerTestFakeLow", ModulePriority::Low);

    ModManager::getInstance().unregistry("ModManagerTestFakeNormal");

    EXPECT_EQ(ModManager::getInstance().getModule("ModManagerTestFakeNormal"), nullptr);
    EXPECT_EQ(ModManager::getInstance().getModule("ModManagerTestFakeHighest"), highest);
    EXPECT_NE(ModManager::getInstance().getModule("ModManagerTestFakeLow"), nullptr);

    std::ptrdiff_t highestIndex = this->IndexOf("ModManagerTestFakeHighest");
    std::ptrdiff_t lowIndex = this->IndexOf("ModManagerTestFakeLow");

    ASSERT_GE(highestIndex, 0);
    ASSERT_GE(lowIndex, 0);
    EXPECT_LT(highestIndex, lowIndex);

    ModManager::getInstance().unregistry("ModManagerTestFakeUnknown");
    EXPECT_EQ(ModManager::getInstance().getModule("ModManagerTestFakeHighest"), highest);
}
