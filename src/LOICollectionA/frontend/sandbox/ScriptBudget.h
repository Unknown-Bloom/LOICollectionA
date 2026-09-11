#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

#include "LOICollectionA/frontend/sandbox/SandboxBudget.h"

namespace LOICollection::frontend::sandbox {
    struct BudgetOverride {
        std::optional<std::size_t> maxInstructions;
        std::optional<std::chrono::milliseconds> maxWallTime;
        std::optional<std::size_t> maxFrames;
        std::optional<std::size_t> maxNativeCalls;
        std::optional<std::size_t> maxObjectCount;
        std::optional<std::size_t> maxArrayElements;
        std::optional<std::size_t> maxStringBytes;
        std::optional<std::size_t> maxTotalBytes;
    };

    class BudgetPolicy {
    public:
        BudgetPolicy() = default;

        void setDefault(const BudgetOverride& override) { mBase = clamp(apply(mBase, override)); }

        void setOverride(const std::string& scriptId, const BudgetOverride& override) {
            mOverrides.insert_or_assign(scriptId, clamp(apply(mBase, override)));
        }

        [[nodiscard]] SandboxBudget resolve(const std::string& scriptId) const {
            if (const auto it = mOverrides.find(scriptId); it != mOverrides.end())
                return it->second;

            return mBase;
        }

        [[nodiscard]] static SandboxBudget apply(SandboxBudget budget, const BudgetOverride& override) {
            if (override.maxInstructions) budget.maxInstructions = *override.maxInstructions;
            if (override.maxWallTime) budget.maxWallTime = *override.maxWallTime;
            if (override.maxFrames) budget.maxFrames = *override.maxFrames;
            if (override.maxNativeCalls) budget.maxNativeCalls = *override.maxNativeCalls;
            if (override.maxObjectCount) budget.maxObjectCount = *override.maxObjectCount;
            if (override.maxArrayElements) budget.maxArrayElements = *override.maxArrayElements;
            if (override.maxStringBytes) budget.maxStringBytes = *override.maxStringBytes;
            if (override.maxTotalBytes) budget.maxTotalBytes = *override.maxTotalBytes;

            return budget;
        }

        [[nodiscard]] static SandboxBudget clamp(const SandboxBudget& budget) {
            SandboxBudget result = budget;

            const std::chrono::milliseconds minWallTime{ 10 };
            const std::chrono::milliseconds maxWallTime{ 30'000 };

            result.maxInstructions = std::clamp<std::size_t>(result.maxInstructions, 10'000, 100'000'000);
            result.maxWallTime = std::clamp(result.maxWallTime, minWallTime, maxWallTime);
            result.maxFrames = std::clamp<std::size_t>(result.maxFrames, 16, 4096);
            result.maxNativeCalls = std::clamp<std::size_t>(result.maxNativeCalls, 1'000, 10'000'000);
            result.maxObjectCount = std::clamp<std::size_t>(result.maxObjectCount, 1'000, 10'000'000);
            result.maxArrayElements = std::clamp<std::size_t>(result.maxArrayElements, 1'000, 10'000'000);
            result.maxStringBytes = std::clamp<std::size_t>(result.maxStringBytes, 4 << 10, 256 << 20);
            result.maxTotalBytes = std::clamp<std::size_t>(result.maxTotalBytes, 1 << 20, 1 << 30);

            return result;
        }

    private:
        SandboxBudget mBase = clamp(SandboxBudget{});
        std::unordered_map<std::string, SandboxBudget> mOverrides;
    };
}
