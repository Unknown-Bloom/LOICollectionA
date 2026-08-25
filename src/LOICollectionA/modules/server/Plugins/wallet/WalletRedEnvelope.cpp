#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include <fmt/core.h>

#include <ll/api/Expected.h>
#include <ll/api/base/Containers.h>
#include <ll/api/event/EventBus.h>
#include <ll/api/service/Bedrock.h>
#include <ll/api/utils/RandomUtils.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>

#include <mc/network/packet/TextPacket.h>

#include "LOICollectionA/include/CallbackUtils.h"
#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

#include "LOICollectionA/include/server/Events/modules/RedEnvelopeCompletedEvent.h"

#include "LOICollectionA/coro/TimerManager.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/wallet/WalletRedEnvelope.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletLedger.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletType.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    struct WalletRedEnvelope::Impl {
        std::shared_ptr<SQLiteStorage> db;
        const Config::C_Wallet& options;
        std::shared_ptr<ll::io::Logger> logger;
        TimerManager& timerManager;

        WalletLedger& ledger;
        TransferProvider transferProvider;

        ll::ConcurrentDenseMap<std::string, std::vector<RedEnvelopeEntry>> mRedEnvelopes;

        Impl(
            std::shared_ptr<SQLiteStorage> db_,
            const Config::C_Wallet& options_,
            std::shared_ptr<ll::io::Logger> logger_,
            TimerManager& timerManager_,
            WalletLedger& ledger_,
            TransferProvider transferProvider_
        ) : db(std::move(db_)),
            options(options_),
            logger(std::move(logger_)),
            timerManager(timerManager_),
            ledger(ledger_),
            transferProvider(std::move(transferProvider_)) {}
    };

    WalletRedEnvelope::WalletRedEnvelope(
        std::shared_ptr<SQLiteStorage> db,
        const Config::C_Wallet& options,
        std::shared_ptr<ll::io::Logger> logger,
        TimerManager& timerManager,
        WalletLedger& ledger,
        TransferProvider transferProvider
    ) : mImpl(std::make_unique<Impl>(std::move(db), options, std::move(logger), timerManager, ledger, std::move(transferProvider))) {}

    WalletRedEnvelope::~WalletRedEnvelope() = default;

    bool WalletRedEnvelope::isValid() const {
        return this->mImpl->db != nullptr;
    }

    ll::Expected<void> WalletRedEnvelope::createTables() {
        return this->mImpl->db->create("RedEnvelope", [](SQLiteStorage::ColumnCallback ctor) -> void {
            ctor("chat_key");
            ctor("sender_uuid");
            ctor("sender_name");
            ctor("capacity");
            ctor("count");
            ctor("people");
            ctor("created_at");
            ctor("expire_at");
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->create("RedEnvelopeGrab", [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("name");
                ctor("amount");
            });
        });
    }

    ll::Expected<void> WalletRedEnvelope::tryGrab(Player& player, const std::string& message) {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        auto it = this->mImpl->mRedEnvelopes.find(message);
        if (it == this->mImpl->mRedEnvelopes.end())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::NotFound));

        std::string uuid = player.getUuid().asString();

        for (auto& entry : it->second) {
            auto result = this->grabEnvelope(player, uuid, entry);
            if (!result.has_value())
                return ll::Unexpected(result.error());

            if (result.value())
                return {};
        }

        return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::RedEnvelopeCompleted));
    }

    int WalletRedEnvelope::computeGiftAmount(int remainingCapacity, int remainingPeople) {
        if (remainingCapacity <= 0)
            return 0;

        if (remainingPeople <= 1)
            return remainingCapacity;

        int upper = std::min(remainingCapacity - (remainingPeople - 1), (remainingCapacity / remainingPeople) * 2);
        upper = std::max(upper, 1);

        return ll::random_utils::rand(1, upper);
    }

    ll::Expected<bool> WalletRedEnvelope::grabEnvelope(Player& player, const std::string& uuid, RedEnvelopeEntry& entry) {
        auto transaction = SQLiteStorageTransaction::create(*this->mImpl->db);
        if (!transaction.has_value())
            return ll::Unexpected(transaction.error());

        auto conn = transaction.value().connection();

        auto data = this->mImpl->db->get(conn, "RedEnvelope", entry.id);
        if (!data.has_value())
            return ll::Unexpected(data.error());

        auto m = data.value();
        if (m.empty())
            return false;

        int capacity = SystemUtils::toInt(m["capacity"], 0);
        int count = SystemUtils::toInt(m["count"], 0);
        int people = SystemUtils::toInt(m["people"], 0);

        if (people >= count)
            return false;

        if (m.contains("targets") && !m.at("targets").empty()) {
            bool inList = false;
            std::string_view targetsView = m.at("targets");
            size_t start = 0;
            while (start <= targetsView.size()) {
                size_t comma = targetsView.find(',', start);
                size_t length = comma == std::string_view::npos ? std::string_view::npos : comma - start;
                if (!targetsView.substr(start, length).empty() && targetsView.substr(start, length) == uuid) {
                    inList = true;
                    break;
                }
                if (comma == std::string_view::npos)
                    break;
                start = comma + 1;
            }

            if (!inList)
                return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::NotInTargetList));
        }

        std::string grabKey = entry.id + ":" + uuid;
        auto grabbed = this->mImpl->db->has(conn, "RedEnvelopeGrab", grabKey);
        if (!grabbed.has_value())
            return ll::Unexpected(grabbed.error());

        if (grabbed.value())
            return false;

        int remainingPeople = count - people;
        bool last = remainingPeople == 1;
        int amount = last ? capacity : this->computeGiftAmount(capacity, remainingPeople);
        if (amount <= 0)
            return false;

        std::unordered_map<std::string, std::string> update = {
            { "capacity", std::to_string(capacity - amount) },
            { "people", std::to_string(people + 1) }
        };

        auto setEnv = this->mImpl->db->set(conn, "RedEnvelope", entry.id, update);
        if (!setEnv.has_value())
            return ll::Unexpected(setEnv.error());

        auto setGrab = this->mImpl->db->set(conn, "RedEnvelopeGrab", grabKey, {
            { "name", player.getRealName() },
            { "amount", std::to_string(amount) }
        });
        if (!setGrab.has_value())
            return ll::Unexpected(setGrab.error());

        bool nowFull = (people + 1) >= count;
        if (nowFull) {
            auto delEnv = this->mImpl->db->del(conn, "RedEnvelope", entry.id);
            if (!delEnv.has_value())
                return ll::Unexpected(delEnv.error());
        }

        auto commit = transaction.value().commit();
        if (!commit.has_value())
            return ll::Unexpected(commit.error());

        ScoreboardUtils::addScore(player, this->mImpl->options.TargetScoreboard, amount);

        this->mImpl->ledger.record(entry.senderUuid, entry.senderName, uuid, player.getRealName(), amount, 0, "redenvelope_grab");

        this->broadcastReceive(entry, player, amount, people + 1);

        if (amount > entry.kingAmount) {
            entry.kingAmount = amount;
            entry.kingUuid = uuid;
            entry.kingName = player.getRealName();
        }

        if (nowFull) {
            this->announceKing(entry);

            ll::event::EventBus::getInstance().publish(LOICollection::server::Events::RedEnvelopeCompletedEvent(
                entry.id,
                entry.senderUuid,
                entry.kingUuid,
                entry.kingName,
                entry.kingAmount,
                entry.total,
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count()
            ));

            auto& entries = this->mImpl->mRedEnvelopes[entry.chatKey];
            entries.erase(std::remove_if(entries.begin(), entries.end(), [&entry](const RedEnvelopeEntry& e) -> bool {
                return e.id == entry.id;
            }), entries.end());
        }

        return true;
    }

    void WalletRedEnvelope::broadcastContent(Player& sender, const std::string& key, const std::string& id, int score, int count) {
        ll::service::getLevel()->forEachPlayer([this, &sender, &key, &id, score, count](Player& target) -> bool {
            LanguagePlugin::getShared()->getLanguage(target)
                .transform([this, &sender, &target, &key, &id, score, count](const std::string& language) -> void {
                    std::string mMessage = LOICollectionAPI::CallbackUtils::getInstance().translate(
                        tr(language, "wallet.tips.redenvelope.content"), sender
                    );

                    TextPacket::createRawMessage(fmt::format(fmt::runtime(mMessage),
                        id, score, count, this->mImpl->options.RedEnvelopeTimeout, key
                    )).sendTo(target);
                })
                .or_else([this](ll::Error e) -> ll::Expected<void> {
                    e.log(*this->mImpl->logger);

                    return {};
                });

            return true;
        });
    }

    void WalletRedEnvelope::broadcastReceive(const RedEnvelopeEntry& entry, Player& player, int amount, int people) {
        ll::service::getLevel()->forEachPlayer([this, &entry, &player, amount, people](Player& target) -> bool {
            LanguagePlugin::getShared()->getLanguage(target)
                .transform([this, &entry, &player, &target, amount, people](const std::string& language) -> void {
                    std::string mMessage = LOICollectionAPI::CallbackUtils::getInstance().translate(
                        tr(language, "wallet.tips.redenvelope.receive"), player
                    );

                    TextPacket::createRawMessage(fmt::format(fmt::runtime(mMessage),
                        entry.id, amount, people, entry.count
                    )).sendTo(target);
                })
                .or_else([this](ll::Error e) -> ll::Expected<void> {
                    e.log(*this->mImpl->logger);

                    return {};
                });

            return true;
        });
    }

    void WalletRedEnvelope::announceKing(RedEnvelopeEntry& entry) {
        ll::service::getLevel()->forEachPlayer([this, &entry](Player& target) -> bool {
            LanguagePlugin::getShared()->getLanguage(target)
                .transform([this, &entry, &target](const std::string& language) -> void {
                    TextPacket::createRawMessage(fmt::format(fmt::runtime(
                        tr(language, "wallet.tips.redenvelope.receive.over")),
                        entry.id, entry.kingName, entry.kingAmount
                    )).sendTo(target);
                })
                .or_else([this](ll::Error e) -> ll::Expected<void> {
                    e.log(*this->mImpl->logger);

                    return {};
                });

            return true;
        });
    }

    ll::Expected<void> WalletRedEnvelope::deleteEnvelope(const std::string& id) {
        auto transaction = SQLiteStorageTransaction::create(*this->mImpl->db);
        if (!transaction.has_value())
            return ll::Unexpected(transaction.error());

        auto conn = transaction.value().connection();

        auto delEnv = this->mImpl->db->del(conn, "RedEnvelope", id);
        if (!delEnv.has_value())
            return ll::Unexpected(delEnv.error());

        auto grabs = this->mImpl->db->list(conn, "RedEnvelopeGrab");
        if (!grabs.has_value())
            return ll::Unexpected(grabs.error());

        std::string prefix = id + ":";
        std::vector<std::string> keys;
        for (const auto& grabKey : grabs.value()) {
            if (grabKey.rfind(prefix, 0) == 0)
                keys.emplace_back(grabKey);
        }

        if (!keys.empty()) {
            auto delGrabs = this->mImpl->db->del(conn, "RedEnvelopeGrab", keys);
            if (!delGrabs.has_value())
                return ll::Unexpected(delGrabs.error());
        }

        auto commit = transaction.value().commit();
        if (!commit.has_value())
            return ll::Unexpected(commit.error());

        return {};
    }

    ll::Expected<bool> WalletRedEnvelope::refundEnvelope(const std::string& id) {
        auto data = this->mImpl->db->get("RedEnvelope", id, "sender_uuid", "");
        if (!data.has_value())
            return ll::Unexpected(data.error());

        if (data.value().empty())
            return false;

        auto capacity = this->mImpl->db->get("RedEnvelope", id, "capacity", "0");
        if (!capacity.has_value())
            return ll::Unexpected(capacity.error());

        auto chatKey = this->mImpl->db->get("RedEnvelope", id, "chat_key", "");
        if (!chatKey.has_value())
            return ll::Unexpected(chatKey.error());

        auto senderName = this->mImpl->db->get("RedEnvelope", id, "sender_name", "");
        if (!senderName.has_value())
            return ll::Unexpected(senderName.error());

        int remaining = SystemUtils::toInt(capacity.value(), 0);
        if (remaining > 0) {
            auto refund = this->mImpl->transferProvider(data.value(), remaining);
            if (!refund.has_value())
                return ll::Unexpected(refund.error());

            this->mImpl->ledger.record("", "", data.value(), senderName.value(), remaining, 0, "redenvelope_refund");
        }

        auto del = this->deleteEnvelope(id);
        if (!del.has_value())
            return ll::Unexpected(del.error());

        auto it = this->mImpl->mRedEnvelopes.find(chatKey.value());
        if (it != this->mImpl->mRedEnvelopes.end()) {
            it->second.erase(std::remove_if(it->second.begin(), it->second.end(), [&id](const RedEnvelopeEntry& e) -> bool {
                return e.id == id;
            }), it->second.end());
        }

        return true;
    }

    void WalletRedEnvelope::announceTimeout(const std::string& id) {
        ll::service::getLevel()->forEachPlayer([this, id](Player& target) -> bool {
            LanguagePlugin::getShared()->getLanguage(target)
                .transform([this, id, &target](const std::string& language) -> void {
                    TextPacket::createRawMessage(
                        fmt::format(fmt::runtime(tr(language, "wallet.tips.redenvelope.timeout")), id)
                    ).sendTo(target);
                })
                .or_else([this](ll::Error e) -> ll::Expected<void> {
                    e.log(*this->mImpl->logger);

                    return {};
                });

            return true;
        });
    }

    ll::Expected<void> WalletRedEnvelope::scheduleRefund(const std::string& id) {
        this->mImpl->timerManager.schedule(id, std::chrono::seconds(this->mImpl->options.RedEnvelopeTimeout), [this, id]() -> void {
            auto refund = this->refundEnvelope(id);
            if (!refund.has_value()) {
                refund.error().log(*this->mImpl->logger);
                return;
            }

            if (!refund.value())
                return;

            this->announceTimeout(id);
        });

        return {};
    }

    ll::Expected<void> WalletRedEnvelope::sweepExpired() {
        auto ids = this->mImpl->db->list("RedEnvelope");
        if (!ids.has_value())
            return ll::Unexpected(ids.error());

        for (const auto& id : ids.value()) {
            auto data = this->mImpl->db->get("RedEnvelope", id);
            if (!data.has_value())
                return ll::Unexpected(data.error());

            auto m = data.value();
            if (m.empty())
                continue;

            int people = SystemUtils::toInt(m["people"], 0);
            int count = SystemUtils::toInt(m["count"], 0);
            long long expireAt = SystemUtils::toLongLong(m["expire_at"], 0);

            long long now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();

            bool completed = people >= count;
            bool expired = now > expireAt;

            if (completed || expired) {
                auto refund = this->refundEnvelope(id);
                if (!refund.has_value())
                    return ll::Unexpected(refund.error());

                continue;
            }

            auto chatKey = m["chat_key"];
            long long remain = expireAt - now;
            int total = m.contains("total") ? SystemUtils::toInt(m["total"], 0) : SystemUtils::toInt(m["capacity"], 0);

            this->mImpl->mRedEnvelopes[chatKey].push_back({
                id,
                chatKey,
                m["sender_uuid"],
                m["sender_name"],
                count,
                expireAt,
                "",
                "",
                0,
                total
            });

            this->mImpl->timerManager.schedule(id, std::chrono::nanoseconds(remain), [this, id]() -> void {
                auto refund = this->refundEnvelope(id);
                if (!refund.has_value()) {
                    refund.error().log(*this->mImpl->logger);
                    return;
                }

                if (!refund.value())
                    return;

                this->announceTimeout(id);
            });
        }

        return {};
    }

    ll::Expected<void> WalletRedEnvelope::refundAll() {
        auto ids = this->mImpl->db->list("RedEnvelope");
        if (!ids.has_value())
            return ll::Unexpected(ids.error());

        for (const auto& id : ids.value()) {
            auto result = this->refundEnvelope(id);
            if (!result.has_value())
                return ll::Unexpected(result.error());
        }

        this->mImpl->mRedEnvelopes.clear();

        return {};
    }

    ll::Expected<void> WalletRedEnvelope::send(Player& player, const std::string& key, int score, int count, const std::vector<std::string>& targets) {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        if (count <= 0 || score <= 0)
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        if (this->mImpl->options.RedEnvelopeMaxCount > 0 && count > this->mImpl->options.RedEnvelopeMaxCount)
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::RedEnvelopeCountExceeded));

        std::string uuid = player.getUuid().asString();

        int total = score * count;
        if (ScoreboardUtils::getScore(player, this->mImpl->options.TargetScoreboard) < total)
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        auto targetUuids = this->resolveTargetUuids(targets);
        if (!targetUuids.has_value())
            return ll::Unexpected(targetUuids.error());

        std::string targetsValue;
        if (this->mImpl->options.RedEnvelopeTargetedEnabled && !targetUuids.value().empty()) {
            for (const auto& targetUuid : targetUuids.value()) {
                if (!targetsValue.empty())
                    targetsValue += ",";
                targetsValue += targetUuid;
            }
        }

        ScoreboardUtils::reduceScore(player, this->mImpl->options.TargetScoreboard, total);

        std::string id = SystemUtils::getCurrentTimestamp();

        long long expire = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count() + static_cast<long long>(this->mImpl->options.RedEnvelopeTimeout) * 1000000000LL;

        std::unordered_map<std::string, std::string> env = {
            { "chat_key", key },
            { "sender_uuid", uuid },
            { "sender_name", player.getRealName() },
            { "capacity", std::to_string(total) },
            { "total", std::to_string(total) },
            { "count", std::to_string(count) },
            { "people", "0" },
            { "created_at", SystemUtils::getNowTime() },
            { "expire_at", std::to_string(expire) }
        };
        if (!targetsValue.empty())
            env["targets"] = targetsValue;

        auto transaction = SQLiteStorageTransaction::create(*this->mImpl->db);
        if (!transaction.has_value()) {
            ScoreboardUtils::addScore(player, this->mImpl->options.TargetScoreboard, total);

            return ll::Unexpected(transaction.error());
        }

        auto conn = transaction.value().connection();
        auto setEnv = this->mImpl->db->set(conn, "RedEnvelope", id, env);
        if (!setEnv.has_value()) {
            ScoreboardUtils::addScore(player, this->mImpl->options.TargetScoreboard, total);

            return ll::Unexpected(setEnv.error());
        }

        auto commit = transaction.value().commit();
        if (!commit.has_value()) {
            ScoreboardUtils::addScore(player, this->mImpl->options.TargetScoreboard, total);

            return ll::Unexpected(commit.error());
        }

        this->mImpl->mRedEnvelopes[key].push_back({
            id,
            key,
            uuid,
            player.getRealName(),
            count,
            expire,
            "",
            "",
            0,
            total
        });

        this->mImpl->ledger.record(uuid, player.getRealName(), "", "", total, 0, "redenvelope_send");

        this->scheduleRefund(id);

        this->broadcastContent(player, key, id, score, count);

        return {};
    }

    ll::Expected<std::vector<std::string>> WalletRedEnvelope::resolveTargetUuids(const std::vector<std::string>& names) {
        std::vector<std::string> uuids;
        if (names.empty())
            return uuids;

        std::unordered_map<std::string, std::string> nameToUuid;

        ll::service::getLevel()->forEachPlayer([&nameToUuid](Player& target) -> bool {
            nameToUuid[target.getRealName()] = target.getUuid().asString();

            return true;
        });

        auto ids = this->mImpl->db->list("Wallet");
        if (ids.has_value() && !ids.value().empty()) {
            auto rows = this->mImpl->db->get("Wallet", ids.value());
            if (rows.has_value()) {
                for (const auto& [uuid, row] : rows.value()) {
                    if (row.contains("name"))
                        nameToUuid[row.at("name")] = uuid;
                }
            }
        }

        for (const auto& name : names) {
            auto it = nameToUuid.find(name);
            if (it != nameToUuid.end())
                uuids.emplace_back(it->second);
        }

        return uuids;
    }

    ll::Expected<std::vector<std::string>> WalletRedEnvelope::getEnvelopeStats(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->db->list("RedEnvelopeGrab")
            .and_then([this, id](const std::vector<std::string>& keys) -> ll::Expected<std::vector<std::string>> {
                std::string prefix = id + ":";
                std::vector<std::string> grabKeys;
                for (const auto& key : keys)
                    if (key.rfind(prefix, 0) == 0)
                        grabKeys.emplace_back(key);

                if (grabKeys.empty())
                    return std::vector<std::string>{};

                return this->mImpl->db->get("RedEnvelopeGrab", grabKeys)
                    .transform([id](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> rows) -> std::vector<std::string> {
                        std::vector<std::pair<std::string, long long>> grabs;
                        for (const auto& [key, row] : rows) {
                            std::string name = row.contains("name") ? row.at("name") : "?";
                            long long amount = row.contains("amount") ? SystemUtils::toLongLong(row.at("amount"), 0) : 0;

                            grabs.emplace_back(name, amount);
                        }

                        std::sort(grabs.begin(), grabs.end(), [](const auto& a, const auto& b) {
                            return a.second > b.second;
                        });

                        std::vector<std::string> result;
                        result.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rinfo.header")), id));

                        for (const auto& [name, amount] : grabs)
                            result.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rinfo.row")), name, amount));

                        if (!grabs.empty()) {
                            const auto& [kingName, kingAmount] = grabs.front();
                            result.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rinfo.king")), kingName, kingAmount));
                        }

                        return result;
                    });
            });
    }
}
