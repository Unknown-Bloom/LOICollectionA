#include <memory>
#include <mutex>
#include <string>
#include <functional>
#include <shared_mutex>
#include <unordered_map>

#include "ll/api/coro/Executor.h"
#include "ll/api/data/CancellableCallback.h"

#include "LOICollectionA/coro/TimerManager.h"

TimerManager::TimerManager(const ll::coro::Executor& executor) : mExecutor(std::ref(executor)) {}
TimerManager::~TimerManager() {
    this->cancelAll();
}

void TimerManager::schedule(std::string id, ll::coro::Duration delay, std::function<void()> callback) {
    std::unique_lock lock(this->mMutex);

    this->cancelUnlocked(id);

    auto handle = this->mExecutor.get().executeAfter(
        [weak = weak_from_this(), id, cb = std::move(callback)]() -> void {
            cb();

            if (auto self = weak.lock(); self)
                self->remove(id);
        }, delay);

    mTimers.emplace(std::move(id), handle);
}

void TimerManager::loopSchedule(std::string id, ll::coro::Duration delay, std::function<void()> callback) {
    std::unique_lock lock(this->mMutex);

    this->cancelUnlocked(id);

    auto handle = this->mExecutor.get().executeAfter(
        this->makeRepeater(id, delay, std::make_shared<std::function<void()>>(std::move(callback))), delay);

    mTimers.emplace(std::move(id), handle);
}

std::function<void()> TimerManager::makeRepeater(
    const std::string&                     id,
    ll::coro::Duration                     delay,
    std::shared_ptr<std::function<void()>> callback
) {
    return [weak = weak_from_this(), id, delay, callback]() -> void {
        (*callback)();

        auto self = weak.lock();
        if (!self)
            return;

        std::unique_lock lock(self->mMutex);

        auto it = self->mTimers.find(id);
        if (it == self->mTimers.end())
            return;

        it->second = self->mExecutor.get().executeAfter(self->makeRepeater(id, delay, callback), delay);
    };
}

bool TimerManager::cancel(const std::string& id) {
    std::unique_lock lock(this->mMutex);

    return this->cancelUnlocked(id);
}

bool TimerManager::has(const std::string& id) const {
    std::shared_lock lock(this->mMutex);

    return this->mTimers.find(id) != this->mTimers.end();
}

void TimerManager::cancelAll() {
    std::unique_lock lock(this->mMutex);

    this->cancelAllUnlocked();
}

void TimerManager::remove(const std::string& id) {
    std::unique_lock lock(this->mMutex);

    this->mTimers.erase(id);
}

void TimerManager::setExecutor(ll::coro::NonNullExecutorRef executor) {
    std::unique_lock lock(this->mMutex);

    this->cancelAllUnlocked();

    this->mExecutor = std::ref(executor);
}

ll::coro::NonNullExecutorRef TimerManager::getExecutor() const {
    std::shared_lock lock(this->mMutex);

    return this->mExecutor;
}

bool TimerManager::cancelUnlocked(const std::string& id) {
    auto it = this->mTimers.find(id);
    if (it != this->mTimers.end()) {
        it->second->cancel();

        mTimers.erase(it);
        return true;
    }

    return false;
}

void TimerManager::cancelAllUnlocked() {
    for (auto& [id, ptr] : this->mTimers)
        ptr->cancel();

    this->mTimers.clear();
}
