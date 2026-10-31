#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <fstream>
#include <filesystem>

#include "LOICollectionA/utils/I18nUtils.h"

class I18nUtilsTest : public testing::Test {
protected:
    I18nUtils* i18n = nullptr;
    std::filesystem::path tempDir;

    void SetUp() override {
        tempDir = std::filesystem::temp_directory_path() / "i18n_test_";
        tempDir += std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count());

        std::filesystem::create_directories(tempDir);

        std::ofstream zh(tempDir / "zh-CN.json");
        zh << R"({"greeting": "你好", "farewell": "再见"})";
        zh.close();

        std::ofstream en(tempDir / "en-US.json");
        en << R"({"greeting": "Hello", "farewell": "Goodbye"})";
        en.close();

        std::ofstream dummy(tempDir / "readme.txt");
        dummy << "ignore me";
        dummy.close();

        I18nUtils::getInstance() = std::make_unique<I18nUtils>(tempDir.string());
        this->i18n = I18nUtils::getInstance().get();
    }

    void TearDown() override {
        I18nUtils::getInstance().reset();

        std::filesystem::remove_all(tempDir);
    }

    inline std::string tr(std::string_view local, std::string_view key) {
        return I18nUtilsTools::tr(local, key);
    }
};

TEST_F(I18nUtilsTest, LoadLocalesFromDirectory) {
    EXPECT_TRUE(this->i18n->has("zh-CN"));
    EXPECT_TRUE(this->i18n->has("en-US"));
    EXPECT_FALSE(this->i18n->has("fr-FR"));
}

TEST_F(I18nUtilsTest, GetTranslationExistingKey) {
    EXPECT_EQ(this->tr("zh-CN", "greeting"), "你好");
    EXPECT_EQ(this->tr("en-US", "greeting"), "Hello");
}

TEST_F(I18nUtilsTest, GetTranslationMissingKeyReturnsKey) {
    EXPECT_EQ(this->i18n->get("zh-CN", "unknown"), "unknown");
    EXPECT_EQ(this->i18n->get("en-US", "missing"), "missing");
}

TEST_F(I18nUtilsTest, GetTranslationMissingLocaleReturnsKey) {
    EXPECT_EQ(this->i18n->get("fr-FR", "greeting"), "greeting");
}

TEST_F(I18nUtilsTest, TrFallsBackToDefaultLocale) {
    this->i18n->defaultLocale = "en-US";

    EXPECT_EQ(this->tr("fr-FR", "greeting"), "Hello");
    EXPECT_EQ(this->tr("fr-FR", "farewell"), "Goodbye");
}

TEST_F(I18nUtilsTest, TrUsesGivenLocaleIfAvailable) {
    this->i18n->defaultLocale = "zh-CN";

    EXPECT_EQ(this->tr("en-US", "greeting"), "Hello");
    EXPECT_EQ(this->tr("zh-CN", "greeting"), "你好");
}

TEST_F(I18nUtilsTest, SetAddsTranslation) {
    this->i18n->set("fr-FR", "greeting", "Bonjour");

    EXPECT_TRUE(this->i18n->has("fr-FR"));
    EXPECT_EQ(this->i18n->get("fr-FR", "greeting"), "Bonjour");

    EXPECT_EQ(this->i18n->get("zh-CN", "greeting"), "你好");
}

TEST_F(I18nUtilsTest, TrDoesNotFallBackWhenLocaleExists) {
    this->i18n->defaultLocale = "en-US";
    this->i18n->set("en-US", "onlyEn", "only");

    EXPECT_EQ(this->tr("zh-CN", "onlyEn"), "onlyEn");
    EXPECT_EQ(this->tr("fr-FR", "onlyEn"), "only");
}

TEST_F(I18nUtilsTest, FindTableExposesLocale) {
    const I18nUtils::Translations* zh = this->i18n->findTable("zh-CN");
    ASSERT_NE(zh, nullptr);
    EXPECT_EQ(zh->at("greeting"), "你好");

    EXPECT_EQ(this->i18n->findTable("fr-FR"), nullptr);
}
