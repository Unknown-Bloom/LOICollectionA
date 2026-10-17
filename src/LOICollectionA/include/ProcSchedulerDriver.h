#pragma once

#include <atomic>

namespace LOICollection::LOICollectionAPI {
    class ProcSchedulerDriver {
    public:
        LOICOLLECTION_A_NDAPI static ProcSchedulerDriver& getInstance();

        LOICOLLECTION_A_API void start();
        LOICOLLECTION_A_API void stop();

    private:
        std::atomic<bool> running_{ false };
    };
}
