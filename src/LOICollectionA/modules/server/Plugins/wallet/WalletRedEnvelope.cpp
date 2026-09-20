#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
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

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/include/server/Plugins/TableSchema.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/wallet/WalletRedEnvelope.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletLedger.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletType.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    struct WalletRedEnvelope::Impl {
        std::shared_ptr<BlockRepository> db;
        std::optional<RedEnvelopeTable> envelope;
        std::optional<RedEnvelopeGrabTable> grab;
        std::optional<WalletTable> wallet;

        const Config::C_Wallet& options;
        std::shared_ptr<ll::io::Logger> logger;
        TimerManager& timerManager;

        WalletLedger& ledger;
        TransferProvider transferProvider;

        ll::ConcurrentDenseMap<std::string, std::vector<RedEnvelopeEntry>> mRedEnvelopes;

        Impl(
            std::shared_ptr<BlockRepository> db_,
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
        std::shared_ptr<BlockRepository> db,
        const Config::C_Wallet& options,
        std::shared_ptr<ll::io::Logger> logger,
        TimerManager& timerManager,
        WalletLedger& ledger,
        TransferProvider transferProvider
    ) : mImpl(std::make_unique<Impl>(std::move(db), options, std::move(logger), timerManager, ledger, std::move(transferProvider))) {}

    WalletRedEnvelope::~WalletRedEnvelope() = default;

    bool WalletRedEnvelope::isValid() const {
        return this->mImpl->db != nullptr && this->mImpl->envelope.has_value()
            && this->mImpl->grab.has_value() && this->mImpl->wallet.has_value();
    }

    ll::Expected<void> WalletRedEnvelope::createTables() {
        return RedEnvelopeTable::open(*this->mImpl->db, "RedEnvelope")
            .and_then([this](RedEnvelopeTable table) -> ll::Expected<void> {
                this->mImpl->envelope.emplace(std::move(table));

                return RedEnvelopeGrabTable::open(*this->mImpl->db, "RedEnvelopeGrab");
            })
            .and_then([this](RedEnvelopeGrabTable table) -> ll::Expected<void> {
                this->mImpl->grab.emplace(std::move(table));

                return WalletTable::open(*this->mImpl->db, "Wallet");
            })
            .transform([this](WalletTable table) -> void {
                this->mImpl->wallet.emplace(std::move(table));
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

        return 1 + ll::random_utils::rand(upper);
    }

    ll::Expected<bool> WalletRedEnvelope::grabEnvelope(Player& player, const std::string& uuid, RedEnvelopeEntry& entry) {
        auto& envelope = *this->mImpl->envelope;
        auto& grab = *this->mImpl->grab;

        auto envelopeBatch = envelope.tx();
        if (!envelopeBatch.has_value())
            return ll::Unexpected(envelopeBatch.error());

        auto grabBatch = grab.tx();
        if (!grabBatch.has_value())
            return ll::Unexpected(grabBatch.error());

        auto& envelopeTx = envelopeBatch.value();
        auto& grabTx = grabBatch.value();

        auto exists = envelopeTx.has(entry.id);
        if (!exists.has_value())
            return ll::Unexpected(exists.error());

        if (!exists.value())
            return false;

        auto capacity = envelopeTx.get<long long>(entry.id, "capacity", 0);
        if (!capacity.has_value())
            return ll::Unexpected(capacity.error());

        auto count = envelopeTx.get<long long>(entry.id, "count", 0);
        if (!count.has_value())
            return ll::Unexpected(count.error());

        auto people = envelopeTx.get<long long>(entry.id, "people", 0);
        if (!people.has_value())
            return ll::Unexpected(people.error());

        if (people.value() >= count.value())
            return false;

        auto targets = envelopeTx.get<std::string>(entry.id, "targets", "");
        if (!targets.has_value())
            return ll::Unexpected(targets.error());

        if (!targets.value().empty()) {
            bool inList = false;
            std::string_view targetsView = targets.value();
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

        auto grabbed = grabTx.has(grabKey);
        if (!grabbed.has_value())
            return ll::Unexpected(grabbed.error());

        if (grabbed.value())
            return false;

        long long remainingPeople = count.value() - people.value();
        bool last = remainingPeople == 1;
        int amount = last ? static_cast<int>(capacity.value())
            : this->computeGiftAmount(static_cast<int>(capacity.value()), static_cast<int>(remainingPeople));
        if (amount <= 0)
            return false;

        auto setCapacity = envelopeTx.set(entry.id, "capacity", capacity.value() - amount);
        if (!setCapacity.has_value())
            return ll::Unexpected(setCapacity.error());

        auto setPeople = envelopeTx.set(entry.id, "people", people.value() + 1);
        if (!setPeople.has_value())
            return ll::Unexpected(setPeople.error());

        auto setName = grabTx.set(grabKey, "name", player.getRealName());
        if (!setName.has_value())
            return ll::Unexpected(setName.error());

        auto setAmount = grabTx.set(grabKey, "amount", static_cast<long long>(amount));
        if (!setAmount.has_value())
            return ll::Unexpected(setAmount.error());

        bool nowFull = (people.value() + 1) >= count.value();
        if (nowFull) {
            auto delEnv = envelopeTx.del(entry.id);
            if (!delEnv.has_value())
                return ll::Unexpected(delEnv.error());
        }

        auto commitGrab = grabTx.commit();
        if (!commitGrab.has_value()) {
            [[maybe_unused]] auto rolledBack = envelopeTx.rollback();

            return ll::Unexpected(commitGrab.error());
        }

        auto commitEnvelope = envelopeTx.commit();
        if (!commitEnvelope.has_value())
            return ll::Unexpected(commitEnvelope.error());

        ScoreboardUtils::addScore(player, this->mImpl->options.TargetScoreboard, amount);

        this->mImpl->ledger.record(entry.senderUuid, entry.senderName, uuid, player.getRealName(), amount, 0, "redenvelope_grab");

        this->broadcastReceive(entry, player, amount, static_cast<int>(people.value()) + 1);

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
                .transform([&entry, &player, &target, amount, people](const std::string& language) -> void {
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
                .transform([&entry, &target](const std::string& language) -> void {
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
        auto& envelope = *this->mImpl->envelope;
        auto& grab = *this->mImpl->grab;

        auto envelopeBatch = envelope.tx();
        if (!envelopeBatch.has_value())
            return ll::Unexpected(envelopeBatch.error());

        auto grabBatch = grab.tx();
        if (!grabBatch.has_value())
            return ll::Unexpected(grabBatch.error());

        auto& envelopeTx = envelopeBatch.value();
        auto& grabTx = grabBatch.value();

        auto delEnv = envelopeTx.del(id);
        if (!delEnv.has_value()) {
            [[maybe_unused]] auto rolledBack = grabTx.rollback();

            return ll::Unexpected(delEnv.error());
        }

        auto grabs = grab.list();
        if (!grabs.has_value()) {
            [[maybe_unused]] auto rolledBack = envelopeTx.rollback();
            [[maybe_unused]] auto rolledBackGrab = grabTx.rollback();

            return ll::Unexpected(grabs.error());
        }

        std::string prefix = id + ":";
        std::vector<std::string> keys;
        for (const auto& grabKey : grabs.value()) {
            if (grabKey.rfind(prefix, 0) == 0)
                keys.emplace_back(grabKey);
        }

        for (const auto& key : keys) {
            auto delGrab = grabTx.del(key);
            if (!delGrab.has_value()) {
                [[maybe_unused]] auto rolledBack = envelopeTx.rollback();
                [[maybe_unused]] auto rolledBackGrab = grabTx.rollback();

                return ll::Unexpected(delGrab.error());
            }
        }

        auto commitEnvelope = envelopeTx.commit();
        if (!commitEnvelope.has_value()) {
            [[maybe_unused]] auto rolledBack = grabTx.rollback();

            return ll::Unexpected(commitEnvelope.error());
        }

        auto commitGrab = grabTx.commit();
        if (!commitGrab.has_value())
            return ll::Unexpected(commitGrab.error());

        return {};
    }

    ll::Expected<bool> WalletRedEnvelope::refundEnvelope(const std::string& id) {
        auto& envelope = *this->mImpl->envelope;

        auto exists = envelope.has(id);
        if (!exists.has_value())
            return ll::Unexpected(exists.error());

        if (!exists.value())
            return false;

        auto senderUuid = envelope.get<std::string>(id, "sender_uuid", "");
        if (!senderUuid.has_value())
            return ll::Unexpected(senderUuid.error());

        if (senderUuid.value().empty())
            return false;

        auto capacity = envelope.get<long long>(id, "capacity", 0);
        if (!capacity.has_value())
            return ll::Unexpected(capacity.error());

        auto chatKey = envelope.get<std::string>(id, "chat_key", "");
        if (!chatKey.has_value())
            return ll::Unexpected(chatKey.error());

        auto senderName = envelope.get<std::string>(id, "sender_name", "");
        if (!senderName.has_value())
            return ll::Unexpected(senderName.error());

        long long remaining = capacity.value();
        if (remaining > 0) {
            auto refund = this->mImpl->transferProvider(senderUuid.value(), static_cast<int>(remaining));
            if (!refund.has_value())
                return ll::Unexpected(refund.error());

            this->mImpl->ledger.record("", "", senderUuid.value(), senderName.value(), remaining, 0, "redenvelope_refund");
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
                .transform([id, &target](const std::string& language) -> void {
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
        auto& envelope = *this->mImpl->envelope;

        auto ids = envelope.list();
        if (!ids.has_value())
            return ll::Unexpected(ids.error());

        for (const auto& id : ids.value()) {
            auto exists = envelope.has(id);
            if (!exists.has_value())
                return ll::Unexpected(exists.error());

            if (!exists.value())
                continue;

            auto people = envelope.get<long long>(id, "people", 0);
            if (!people.has_value())
                return ll::Unexpected(people.error());

            auto count = envelope.get<long long>(id, "count", 0);
            if (!count.has_value())
                return ll::Unexpected(count.error());

            auto expireAt = envelope.get<long long>(id, "expire_at", 0);
            if (!expireAt.has_value())
                return ll::Unexpected(expireAt.error());

            long long now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();

            bool completed = people.value() >= count.value();
            bool expired = now > expireAt.value();

            if (completed || expired) {
                auto refund = this->refundEnvelope(id);
                if (!refund.has_value())
                    return ll::Unexpected(refund.error());

                continue;
            }

            auto chatKey = envelope.get<std::string>(id, "chat_key", "");
            if (!chatKey.has_value())
                return ll::Unexpected(chatKey.error());

            auto senderUuid = envelope.get<std::string>(id, "sender_uuid", "");
            if (!senderUuid.has_value())
                return ll::Unexpected(senderUuid.error());

            auto senderName = envelope.get<std::string>(id, "sender_name", "");
            if (!senderName.has_value())
                return ll::Unexpected(senderName.error());

            auto total = envelope.get<long long>(id, "total", 0);
            if (!total.has_value())
                return ll::Unexpected(total.error());

            auto capacity = envelope.get<long long>(id, "capacity", 0);
            if (!capacity.has_value())
                return ll::Unexpected(capacity.error());

            long long remain = expireAt.value() - now;
            long long envelopeTotal = total.value() > 0 ? total.value() : capacity.value();

            this->mImpl->mRedEnvelopes[chatKey.value()].push_back({
                id,
                chatKey.value(),
                senderUuid.value(),
                senderName.value(),
                static_cast<int>(count.value()),
                expireAt.value(),
                "",
                "",
                0,
                static_cast<int>(envelopeTotal)
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
        auto& envelope = *this->mImpl->envelope;

        auto ids = envelope.list();
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

        long long total = static_cast<long long>(score) * static_cast<long long>(count);
        if (static_cast<long long>(ScoreboardUtils::getScore(player, this->mImpl->options.TargetScoreboard)) < total)
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

        ScoreboardUtils::reduceScore(player, this->mImpl->options.TargetScoreboard, static_cast<int>(total));

        std::string id = SystemUtils::getCurrentTimestamp();

        long long expire = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count() + static_cast<long long>(this->mImpl->options.RedEnvelopeTimeout) * 1000000000LL;

        auto batch = this->mImpl->envelope->tx();
        if (!batch.has_value()) {
            ScoreboardUtils::addScore(player, this->mImpl->options.TargetScoreboard, static_cast<int>(total));

            return ll::Unexpected(batch.error());
        }

        auto& tx = batch.value();

        auto written = tx.set(id, "chat_key", key)
            .and_then([&tx, &id, &uuid]() -> ll::Expected<void> {
                return tx.set(id, "sender_uuid", uuid);
            })
            .and_then([&tx, &id, &player]() -> ll::Expected<void> {
                return tx.set(id, "sender_name", player.getRealName());
            })
            .and_then([&tx, &id, total]() -> ll::Expected<void> {
                return tx.set(id, "capacity", total);
            })
            .and_then([&tx, &id, total]() -> ll::Expected<void> {
                return tx.set(id, "total", total);
            })
            .and_then([&tx, &id, count]() -> ll::Expected<void> {
                return tx.set(id, "count", static_cast<long long>(count));
            })
            .and_then([&tx, &id]() -> ll::Expected<void> {
                return tx.set(id, "people", 0LL);
            })
            .and_then([&tx, &id, expire]() -> ll::Expected<void> {
                return tx.set(id, "expire_at", expire);
            })
            .and_then([&tx, &id, &targetsValue]() -> ll::Expected<void> {
                if (targetsValue.empty())
                    return {};

                return tx.set(id, "targets", targetsValue);
            });
        if (!written.has_value()) {
            ScoreboardUtils::addScore(player, this->mImpl->options.TargetScoreboard, static_cast<int>(total));

            return ll::Unexpected(written.error());
        }

        auto commit = tx.commit();
        if (!commit.has_value()) {
            ScoreboardUtils::addScore(player, this->mImpl->options.TargetScoreboard, static_cast<int>(total));

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
            static_cast<int>(total)
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

        auto& wallet = *this->mImpl->wallet;

        auto ids = wallet.list();
        if (!ids.has_value())
            return ll::Unexpected(ids.error());

        for (const auto& uuid : ids.value()) {
            auto name = wallet.get<std::string>(uuid, "name", "");
            if (!name.has_value())
                return ll::Unexpected(name.error());

            if (!name.value().empty())
                nameToUuid[name.value()] = uuid;
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

        auto& grab = *this->mImpl->grab;

        auto keys = grab.list();
        if (!keys.has_value())
            return ll::Unexpected(keys.error());

        std::string prefix = id + ":";
        std::vector<std::string> grabKeys;
        for (const auto& key : keys.value())
            if (key.rfind(prefix, 0) == 0)
                grabKeys.emplace_back(key);

        if (grabKeys.empty())
            return std::vector<std::string>{};

        std::vector<std::pair<std::string, long long>> grabs;
        grabs.reserve(grabKeys.size());

        for (const auto& key : grabKeys) {
            auto name = grab.get<std::string>(key, "name", "?");
            if (!name.has_value())
                return ll::Unexpected(name.error());

            auto amount = grab.get<long long>(key, "amount", 0);
            if (!amount.has_value())
                return ll::Unexpected(amount.error());

            grabs.emplace_back(name.value(), amount.value());
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
    }
}
