#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

#include <ll/api/io/LoggerRegistry.h>

#include "LOICollectionA/coro/TimerManager.h"
#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/include/server/Plugins/types/wallet/WalletSchema.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletLedger.h"

#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "common/coro/MockExecutor.h"
#include "common/data/StorageTestSupport.h"

using namespace LOICollection::server::Plugins;
using LOICollection::data::FindMode;

namespace wallet_ledger_test_support {
    constexpr long long kDayNs = 86'400LL * 1'000'000'000LL;

    long long nowNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    bool seed(
        WalletLedgerTable& table,
        const std::string& id,
        const std::string& type,
        const std::string& fromUuid,
        const std::string& fromName,
        const std::string& toUuid,
        const std::string& toName,
        long long amount,
        long long fee,
        long long timeNs
    ) {
        return table.set(id, WalletLedgerCol::from_uuid, fromUuid).has_value()
            && table.set(id, WalletLedgerCol::from_name, fromName).has_value()
            && table.set(id, WalletLedgerCol::to_uuid, toUuid).has_value()
            && table.set(id, WalletLedgerCol::to_name, toName).has_value()
            && table.set(id, WalletLedgerCol::amount, amount).has_value()
            && table.set(id, WalletLedgerCol::fee, fee).has_value()
            && table.set(id, WalletLedgerCol::type, type).has_value()
            && table.set(id, WalletLedgerCol::time_ns, timeNs).has_value()
            && table.set(id, WalletLedgerCol::time, SystemUtils::getNowTime()).has_value();
    }
}

class WalletLedgerTest : public testing::Test {
protected:
    void SetUp() override {
        this->mTimerManager = std::make_shared<TimerManager>(this->mExecutor);
        this->mPath = storage_test_support::tempDb("wallet-ledger", "daily");

        auto repo = BlockRepository::open(this->mPath.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        this->mRepo = repo.value();

        this->mLogger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        ASSERT_NE(this->mLogger, nullptr);

        this->mOptions.WalletHistoryEnabled = true;
        this->mOptions.WalletHistoryRetentionDays = 1;
    }

    void TearDown() override {
        this->mRepo.reset();
        storage_test_support::dropDb(this->mPath);
    }

    MockExecutor mExecutor;
    std::shared_ptr<TimerManager> mTimerManager;
    std::filesystem::path mPath;
    std::shared_ptr<BlockRepository> mRepo;
    std::shared_ptr<ll::io::Logger> mLogger;
    Config::C_Wallet mOptions;
};

TEST_F(WalletLedgerTest, RecordUpdatesDailyFolds) {
    WalletLedger ledger(this->mRepo, this->mOptions, this->mLogger, *this->mTimerManager);

    auto created = ledger.createTables();
    ASSERT_TRUE(created.has_value()) << created.error().message();

    ledger.record("u1", "alice", "u2", "bob", 100, 5, "transfer");
    ledger.record("u1", "alice", "u3", "carol", 20, 2, "transfer");
    ledger.record("u1", "alice", "bank", "bank", 7, 0, "bank_deposit");
    ledger.record("u1", "alice", "", "", 50, 0, "redenvelope_send");
    ledger.record("u9", "dave", "u3", "carol", 50, 0, "redenvelope_grab");

    auto outgoing = ledger.getTodayOutgoing("u1");
    ASSERT_TRUE(outgoing.has_value()) << outgoing.error().message();
    EXPECT_EQ(outgoing.value(), 127);

    auto untouched = ledger.getTodayOutgoing("u2");
    ASSERT_TRUE(untouched.has_value()) << untouched.error().message();
    EXPECT_EQ(untouched.value(), 0);

    auto stats = ledger.getRedEnvelopeDailyStats();
    ASSERT_TRUE(stats.has_value()) << stats.error().message();
    EXPECT_EQ(stats->size(), 5u);
}

TEST_F(WalletLedgerTest, ReconcileRebuildsTodayOnOpen) {
    auto table = WalletLedgerTable::open(*this->mRepo, "WalletLedger");
    ASSERT_TRUE(table.has_value()) << table.error().message();

    long long at = wallet_ledger_test_support::nowNs();

    ASSERT_TRUE(wallet_ledger_test_support::seed(
        table.value(), "today_transfer", "transfer", "u1", "alice", "u2", "bob", 30, 2, at));
    ASSERT_TRUE(wallet_ledger_test_support::seed(
        table.value(), "today_send", "redenvelope_send", "u1", "alice", "", "", 40, 0, at));
    ASSERT_TRUE(wallet_ledger_test_support::seed(
        table.value(), "today_grab", "redenvelope_grab", "u9", "dave", "u3", "carol", 40, 0, at));
    ASSERT_TRUE(wallet_ledger_test_support::seed(
        table.value(), "yesterday_transfer", "transfer", "u1", "alice", "u2", "bob", 500, 0,
        at - wallet_ledger_test_support::kDayNs));

    WalletLedger ledger(this->mRepo, this->mOptions, this->mLogger, *this->mTimerManager);

    auto created = ledger.createTables();
    ASSERT_TRUE(created.has_value()) << created.error().message();

    auto outgoing = ledger.getTodayOutgoing("u1");
    ASSERT_TRUE(outgoing.has_value()) << outgoing.error().message();
    EXPECT_EQ(outgoing.value(), 32);

    auto stats = ledger.getRedEnvelopeDailyStats();
    ASSERT_TRUE(stats.has_value()) << stats.error().message();
    EXPECT_EQ(stats->size(), 5u);
}

TEST_F(WalletLedgerTest, CleanupDropsExpiredLedgerRows) {
    auto table = WalletLedgerTable::open(*this->mRepo, "WalletLedger");
    ASSERT_TRUE(table.has_value()) << table.error().message();

    long long at = wallet_ledger_test_support::nowNs();

    ASSERT_TRUE(wallet_ledger_test_support::seed(
        table.value(), "expired", "transfer", "uOld", "old", "u2", "bob", 10, 0,
        at - 2 * wallet_ledger_test_support::kDayNs));
    ASSERT_TRUE(wallet_ledger_test_support::seed(
        table.value(), "fresh", "transfer", "uNew", "new", "u2", "bob", 10, 0, at));

    WalletLedger ledger(this->mRepo, this->mOptions, this->mLogger, *this->mTimerManager);

    auto created = ledger.createTables();
    ASSERT_TRUE(created.has_value()) << created.error().message();

    ledger.startCleanupSchedule();
    this->mExecutor.advanceTime(std::chrono::hours(25));

    auto expired = table->find(FindMode::And, { { WalletLedgerCol::from_uuid, std::string("uOld") } });
    ASSERT_TRUE(expired.has_value()) << expired.error().message();
    EXPECT_TRUE(expired->empty());

    auto fresh = table->find(FindMode::And, { { WalletLedgerCol::from_uuid, std::string("uNew") } });
    ASSERT_TRUE(fresh.has_value()) << fresh.error().message();
    EXPECT_FALSE(fresh->empty());
}
