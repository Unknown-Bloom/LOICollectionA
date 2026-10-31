#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

#include <fmt/core.h>

#include <ll/api/Expected.h>
#include <ll/api/event/EventBus.h>

#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/include/server/Events/modules/WalletTransferEvent.h"

#include "LOICollectionA/coro/TimerManager.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/data/sqlite/block/TimeFold.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/types/wallet/WalletSchema.h"

#include "LOICollectionA/include/server/Plugins/wallet/WalletLedger.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    using LOICollection::data::FindMode;
    using LOICollection::data::TimeBucket;
    using LOICollection::data::TimeFold;
    using LOICollection::data::TimeFoldValue;

    struct WalletLedger::Impl {
        std::shared_ptr<BlockRepository> db;
        const Config::C_Wallet& options;
        std::shared_ptr<ll::io::Logger> logger;
        TimerManager& timerManager;

        std::optional<WalletLedgerTable> ledger;
        std::optional<WalletFeeTable> fee;
        std::optional<TimeFold> outDaily;
        std::optional<TimeFold> sendDaily;
        std::optional<TimeFold> grabDaily;

        std::atomic<uint64_t> mLedgerSeq{ 0 };

        Impl(
            std::shared_ptr<BlockRepository> db_,
            const Config::C_Wallet& options_,
            std::shared_ptr<ll::io::Logger> logger_,
            TimerManager& timerManager_
        ) : db(std::move(db_)),
            options(options_),
            logger(std::move(logger_)),
            timerManager(timerManager_) {}
    };

    WalletLedger::WalletLedger(
        std::shared_ptr<BlockRepository> db,
        const Config::C_Wallet& options,
        std::shared_ptr<ll::io::Logger> logger,
        TimerManager& timerManager
    ) : mImpl(std::make_unique<Impl>(std::move(db), options, std::move(logger), timerManager)) {}

    WalletLedger::~WalletLedger() = default;

    bool WalletLedger::isValid() const {
        return this->mImpl->db != nullptr
            && this->mImpl->ledger.has_value()
            && this->mImpl->fee.has_value()
            && this->mImpl->outDaily.has_value()
            && this->mImpl->sendDaily.has_value()
            && this->mImpl->grabDaily.has_value();
    }

    ll::Expected<void> WalletLedger::createTables() {
        return WalletLedgerTable::open(*this->mImpl->db, "WalletLedger")
            .and_then([this](WalletLedgerTable t) {
                this->mImpl->ledger.emplace(std::move(t));
                return WalletFeeTable::open(*this->mImpl->db, "WalletFee");
            })
            .and_then([this](WalletFeeTable t) -> ll::Expected<void> {
                this->mImpl->fee.emplace(std::move(t));

                return TimeFold::open(*this->mImpl->db, "WalletOutDaily", TimeBucket::Day)
                    .and_then([this](TimeFold fold) {
                        this->mImpl->outDaily.emplace(std::move(fold));
                        return TimeFold::open(*this->mImpl->db, "WalletEnvelopeSendDaily", TimeBucket::Day);
                    })
                    .and_then([this](TimeFold fold) {
                        this->mImpl->sendDaily.emplace(std::move(fold));
                        return TimeFold::open(*this->mImpl->db, "WalletEnvelopeGrabDaily", TimeBucket::Day);
                    })
                    .transform([this](TimeFold fold) -> void {
                        this->mImpl->grabDaily.emplace(std::move(fold));
                    });
            })
            .and_then([this]() -> ll::Expected<void> {
                return this->reconcileToday();
            });
    }

    void WalletLedger::record(const std::string& fromUuid, const std::string& fromName, const std::string& toUuid, const std::string& toName, long long amount, long long fee, const std::string& type) {
        ll::event::EventBus::getInstance().publish(LOICollection::server::Events::WalletTransferEvent(
            fromUuid,
            fromName,
            toUuid,
            toName,
            amount,
            fee,
            type,
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        ));

        if (!this->isValid() || !this->mImpl->options.WalletHistoryEnabled)
            return;

        long long nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        std::string id = std::to_string(nowNs) + "_" + std::to_string(this->mImpl->mLedgerSeq.fetch_add(1, std::memory_order_relaxed));

        auto& ledger = *this->mImpl->ledger;

        auto tx = ledger.tx();
        if (!tx.has_value()) {
            tx.error().log(*this->mImpl->logger);
            return;
        }

        auto& batch = tx.value();

        batch.set(id, WalletLedgerCol::from_uuid, fromUuid)
            .and_then([&]() { return batch.set(id, WalletLedgerCol::from_name, fromName); })
            .and_then([&]() { return batch.set(id, WalletLedgerCol::to_uuid, toUuid); })
            .and_then([&]() { return batch.set(id, WalletLedgerCol::to_name, toName); })
            .and_then([&]() { return batch.set(id, WalletLedgerCol::amount, amount); })
            .and_then([&]() { return batch.set(id, WalletLedgerCol::fee, fee); })
            .and_then([&]() { return batch.set(id, WalletLedgerCol::type, type); })
            .and_then([&]() { return batch.set(id, WalletLedgerCol::time_ns, nowNs); })
            .and_then([&]() { return batch.set(id, WalletLedgerCol::time, SystemUtils::getNowTime()); })
            .and_then([&]() -> ll::Expected<void> {
                if (type == "transfer")
                    return this->mImpl->outDaily->add(batch.writeBatch(), fromUuid, nowNs, amount + fee);

                if (type == "redenvelope_send")
                    return this->mImpl->sendDaily->add(batch.writeBatch(), fromName, nowNs, amount);

                if (type == "redenvelope_grab")
                    return this->mImpl->grabDaily->add(batch.writeBatch(), toName, nowNs, amount);

                return {};
            })
            .and_then([&]() { return batch.commit().transform([](bool) {}); })
            .or_else([this](ll::Error e) -> ll::Expected<void> {
                e.log(*this->mImpl->logger);

                return {};
            });
    }

    ll::Expected<std::vector<std::string>> WalletLedger::getPlayerLedger(const std::string& uuid, int limit) {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->ledger->find(FindMode::Or, {
            { WalletLedgerCol::from_uuid, uuid },
            { WalletLedgerCol::to_uuid, uuid }
        }).and_then([this, uuid, limit](const std::vector<std::string>& ids) -> ll::Expected<std::vector<std::string>> {
            if (ids.empty())
                return std::vector<std::string>{};

            std::vector<std::pair<std::string, std::string>> sorted;
            sorted.reserve(ids.size());

            for (const auto& id : ids) {
                auto row = this->mImpl->ledger->getRow(id);
                if (!row.has_value())
                    return ll::Unexpected(row.error());

                const auto& fields = row.value();

                std::string type = fields.contains("type") ? fields.at("type") : "";
                std::string fromName = fields.contains("from_name") ? fields.at("from_name") : "";
                std::string toName = fields.contains("to_name") ? fields.at("to_name") : "";
                std::string amount = fields.contains("amount") ? fields.at("amount") : "0";
                std::string fee = fields.contains("fee") ? fields.at("fee") : "0";
                std::string timeNs = fields.contains("time_ns") ? fields.at("time_ns") : "";
                std::string timeStr = fields.contains("time") ? fields.at("time") : "";

                bool isOut = fields.contains("from_uuid") && fields.at("from_uuid") == uuid;
                std::string direction = tr({}, isOut ? "wallet.history.type.out" : "wallet.history.type.in");

                std::string display = fmt::format(fmt::runtime(tr({}, "wallet.history.row")),
                    timeStr, direction, fromName, toName, amount, fee);

                sorted.emplace_back(timeNs, display);
            }

            std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
                return a.first > b.first;
            });

            if (limit > 0 && sorted.size() > static_cast<size_t>(limit))
                sorted.resize(static_cast<size_t>(limit));

            std::vector<std::string> result;
            result.reserve(sorted.size());
            for (const auto& [t, d] : sorted)
                result.emplace_back(d);

            return result;
        });
    }

    ll::Expected<void> WalletLedger::sendHistory(Player& receiver, const std::string& uuid, const std::string& name, int limit) {
        return this->getPlayerLedger(uuid, limit)
            .and_then([&receiver, name](const std::vector<std::string>& lines) -> ll::Expected<void> {
                if (lines.empty()) {
                    receiver.sendMessage(tr({}, "wallet.history.empty"));

                    return {};
                }

                receiver.sendMessage(fmt::format(fmt::runtime(tr({}, "wallet.history.header")), name));

                for (const auto& line : lines)
                    receiver.sendMessage(line);

                return {};
            });
    }

    ll::Expected<long long> WalletLedger::getFeePool() {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->fee->get<long long>("total", WalletFeeCol::amount, 0);
    }

    ll::Expected<void> WalletLedger::accumulateFee(long long amount) {
        if (amount <= 0)
            return {};

        return this->mImpl->fee->get<long long>("total", WalletFeeCol::amount, 0)
            .and_then([this, amount](long long total) -> ll::Expected<void> {
                return this->mImpl->fee->set("total", WalletFeeCol::amount, total + amount);
            });
    }

    ll::Expected<long long> WalletLedger::getTodayOutgoing(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        return this->mImpl->outDaily->get(uuid, nowNs)
            .transform([](TimeFoldValue const& value) -> long long {
                return value.sum;
            });
    }

    ll::Expected<std::vector<std::string>> WalletLedger::getRedEnvelopeDailyStats() {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        auto sends = this->mImpl->sendDaily->groups(nowNs);
        if (!sends.has_value())
            return ll::Unexpected(sends.error());

        auto grabs = this->mImpl->grabDaily->groups(nowNs);
        if (!grabs.has_value())
            return ll::Unexpected(grabs.error());

        long long sendCount = 0;
        long long sendTotal = 0;
        std::string topSender;
        long long topSenderAmount = 0;

        for (auto const& [name, value] : sends.value()) {
            sendCount += value.count;
            sendTotal += value.sum;

            if (value.sum > topSenderAmount) {
                topSenderAmount = value.sum;
                topSender = name;
            }
        }

        std::vector<std::pair<std::string, long long>> topGrabbers;
        topGrabbers.reserve(grabs.value().size());
        for (auto const& [name, value] : grabs.value())
            topGrabbers.emplace_back(name, value.sum);

        std::sort(topGrabbers.begin(), topGrabbers.end(), [](auto const& left, auto const& right) {
            return left.second > right.second;
        });
        if (topGrabbers.size() > 5)
            topGrabbers.resize(5);

        std::vector<std::string> result;
        result.emplace_back(tr({}, "wallet.rstat.header"));
        result.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rstat.count")), sendCount));
        result.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rstat.total")), sendTotal));
        result.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rstat.generous")), topSender, topSenderAmount));

        for (size_t i = 0; i < topGrabbers.size(); ++i)
            result.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rstat.grabber")),
                i + 1, topGrabbers.at(i).first, topGrabbers.at(i).second));

        return result;
    }

    ll::Expected<void> WalletLedger::reconcileToday() {
        constexpr long long NS_PER_DAY = 86400LL * 1000000000LL;

        long long nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        long long todayStartNs = (nowNs / NS_PER_DAY) * NS_PER_DAY;

        auto keys = this->mImpl->ledger->range(
            WalletLedgerCol::time_ns, todayStartNs, todayStartNs + NS_PER_DAY - 1
        );
        if (!keys.has_value())
            return ll::Unexpected(keys.error());

        std::unordered_map<std::string, TimeFoldValue> outgoing;
        std::unordered_map<std::string, TimeFoldValue> sent;
        std::unordered_map<std::string, TimeFoldValue> grabbed;

        auto accumulate = [](std::unordered_map<std::string, TimeFoldValue>& target,
                             const std::string& group, long long value) -> void {
            TimeFoldValue& stat = target[group];
            if (stat.count == 0) {
                stat.min = value;
                stat.max = value;
            } else {
                stat.min = std::min(stat.min, value);
                stat.max = std::max(stat.max, value);
            }

            stat.count += 1;
            stat.sum += value;
        };

        for (auto const& key : keys.value()) {
            auto row = this->mImpl->ledger->getRow(key);
            if (!row.has_value())
                return ll::Unexpected(row.error());

            auto const& fields = row.value();
            std::string type = fields.contains("type") ? fields.at("type") : "";
            long long amount = SystemUtils::toLongLong(fields.contains("amount") ? fields.at("amount") : "", 0);
            long long fee = SystemUtils::toLongLong(fields.contains("fee") ? fields.at("fee") : "", 0);

            if (type == "transfer") {
                accumulate(outgoing, fields.contains("from_uuid") ? fields.at("from_uuid") : "", amount + fee);
            } else if (type == "redenvelope_send") {
                accumulate(sent, fields.contains("from_name") ? fields.at("from_name") : "?", amount);
            } else if (type == "redenvelope_grab") {
                accumulate(grabbed, fields.contains("to_name") ? fields.at("to_name") : "?", amount);
            }
        }

        auto toEntries = [](auto const& values) {
            std::vector<std::pair<std::string, TimeFoldValue>> entries;
            entries.reserve(values.size());
            for (auto const& [group, value] : values)
                entries.emplace_back(group, value);

            return entries;
        };

        return this->mImpl->outDaily->replace(todayStartNs, toEntries(outgoing))
            .and_then([this, todayStartNs, &toEntries, &sent]() -> ll::Expected<void> {
                return this->mImpl->sendDaily->replace(todayStartNs, toEntries(sent));
            })
            .and_then([this, todayStartNs, &toEntries, &grabbed]() -> ll::Expected<void> {
                return this->mImpl->grabDaily->replace(todayStartNs, toEntries(grabbed));
            });
    }

    void WalletLedger::startCleanupSchedule() {
        if (this->mImpl->options.WalletHistoryRetentionDays <= 0)
            return;

        this->scheduleCleanup();
    }

    void WalletLedger::scheduleCleanup() {
        this->mImpl->timerManager.schedule("wallet_ledger_cleanup", std::chrono::hours(24), [this]() -> void {
            this->cleanup();
        });
    }

    void WalletLedger::cleanup() {
        long long cutoff = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()
            - static_cast<long long>(this->mImpl->options.WalletHistoryRetentionDays) * 86400LL * 1000000000LL;

        auto expired = this->mImpl->ledger->range(
            WalletLedgerCol::time_ns, std::numeric_limits<long long>::min(), cutoff - 1
        );
        if (!expired.has_value()) {
            expired.error().log(*this->mImpl->logger);
            this->scheduleCleanup();
            return;
        }

        auto tx = this->mImpl->ledger->tx();
        if (!tx.has_value()) {
            tx.error().log(*this->mImpl->logger);
            this->scheduleCleanup();
            return;
        }

        auto& batch = tx.value();

        for (const auto& id : expired.value()) {
            auto r = batch.del(id);
            if (!r.has_value())
                r.error().log(*this->mImpl->logger);
        }

        auto committed = batch.commit();
        if (!committed.has_value())
            committed.error().log(*this->mImpl->logger);

        for (TimeFold* fold : {
            &*this->mImpl->outDaily,
            &*this->mImpl->sendDaily,
            &*this->mImpl->grabDaily
        }) {
            fold->trim(cutoff).or_else([this](ll::Error e) -> ll::Expected<void> {
                e.log(*this->mImpl->logger);

                return {};
            });
        }

        this->mImpl->db->exec("VACUUM;")
            .or_else([this](ll::Error e) -> ll::Expected<void> {
                e.log(*this->mImpl->logger);

                return {};
            });

        this->scheduleCleanup();
    }
}
