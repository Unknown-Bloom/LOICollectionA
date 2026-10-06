#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <functional>
#include <shared_mutex>
#include <unordered_map>

#include "ll/api/coro/Executor.h"
#include "ll/api/data/CancellableCallback.h"

#include "LOICollectionA/base/Macro.h"

class TimerManager : public std::enable_shared_from_this<TimerManager> {
public:
    LOICOLLECTION_A_API   explicit TimerManager(ll::coro::NonNullExecutorRef executor);
    LOICOLLECTION_A_API   ~TimerManager();

    LOICOLLECTION_A_API   void schedule(std::string id, ll::coro::Duration delay, std::function<void()> callback);
    LOICOLLECTION_A_API   void loopSchedule(std::string id, ll::coro::Duration delay, std::function<void()> callback);

    LOICOLLECTION_A_API   bool cancel(const std::string& id);
    LOICOLLECTION_A_NDAPI bool has(const std::string& id) const;

    LOICOLLECTION_A_API   void cancelAll();
    LOICOLLECTION_A_API   void remove(const std::string& id);

    LOICOLLECTION_A_API   void setExecutor(ll::coro::NonNullExecutorRef executor);

    LOICOLLECTION_A_NDAPI ll::coro::NonNullExecutorRef getExecutor() const;

private:
    std::function<void()> makeRepeater(
        const std::string&                            id,
        ll::coro::Duration                            delay,
        std::shared_ptr<std::function<void()>>        callback
    );

    bool cancelUnlocked(const std::string& id);
    void cancelAllUnlocked();

    mutable std::shared_mutex mMutex;

    std::reference_wrapper<const ll::coro::Executor> mExecutor;

    std::unordered_map<std::string, std::shared_ptr<ll::data::CancellableCallback>> mTimers;
};
