#pragma once

#include <atomic>
#include <memory>

namespace LOICollection::frontend {
    class CancelToken {
    public:
        CancelToken() : mFlag(std::make_shared<std::atomic<bool>>(false)) {}

        void cancel() { mFlag->store(true, std::memory_order_relaxed); }

        [[nodiscard]] bool cancelled() const {
            return mFlag->load(std::memory_order_relaxed);
        }

    private:
        std::shared_ptr<std::atomic<bool>> mFlag;
    };
}
