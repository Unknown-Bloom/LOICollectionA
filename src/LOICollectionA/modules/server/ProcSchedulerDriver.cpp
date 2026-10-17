#include "LOICollectionA/include/ProcSchedulerDriver.h"

#include <ll/api/chrono/GameChrono.h>
#include <ll/api/coro/CoroTask.h>
#include <ll/api/thread/ServerThreadExecutor.h>

#include "LOICollectionA/frontend/runtime/Scheduler.h"

namespace LOICollection::LOICollectionAPI {
    void ProcSchedulerDriver::start() {
        if (running_.exchange(true, std::memory_order_acq_rel))
            return;

        ll::coro::keepThis([this]() -> ll::coro::CoroTask<> {
            while (running_.load(std::memory_order_acquire)) {
                co_await ll::chrono::ticks{1};
                frontend::runtime::ProcScheduler::getInstance().pump();
            }
        }).launch(ll::thread::ServerThreadExecutor::getDefault());
    }

    void ProcSchedulerDriver::stop() {
        running_.store(false, std::memory_order_release);
    }
}
