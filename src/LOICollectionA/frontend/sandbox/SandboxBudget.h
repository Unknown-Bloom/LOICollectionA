#pragma once

#include <chrono>
#include <cstddef>

namespace LOICollection::frontend::sandbox {

    // Per-execution resource budget.  The VM consults this struct every
    // iteration of its main loop; exceeding any limit aborts the run with a
    // diagnostic instead of letting a hostile script pin the host.
    struct SandboxBudget {
        // --- configurable limits ---
        std::size_t maxInstructions = 1'000'000;
        std::chrono::milliseconds maxWallTime{ 1000 };
        std::size_t maxFrames = 256;
        std::size_t maxNativeCalls = 100'000;
        std::size_t maxObjectCount = 100'000;
        std::size_t maxArrayElements = 100'000;
        std::size_t maxStringBytes = 1 << 20;   // 1 MiB per string
        std::size_t maxTotalBytes = 64 << 20;   // 64 MiB, best-effort estimate

        // --- runtime accounting (reset by reset()) ---
        std::chrono::steady_clock::time_point startTime{};
        std::size_t executedInstructions = 0;
        std::size_t nativeCallCount = 0;
        std::size_t objectCount = 0;
        std::size_t allocatedBytes = 0;

        void reset() {
            startTime = std::chrono::steady_clock::now();
            executedInstructions = 0;
            nativeCallCount = 0;
            objectCount = 0;
            allocatedBytes = 0;
        }

        enum class Violation {
            None,
            InstructionLimit,
            WallTimeLimit,
            NativeCallLimit,
            ObjectCountLimit,
            ArrayElementLimit,
            StringByteLimit,
            TotalByteLimit,
        };

        // Advance the instruction counter and, every 1024 instructions, sample
        // the wall clock.  Sampling is throttled so the steady_clock call stays
        // off the hot path.
        Violation tickInstruction() {
            if (++executedInstructions > maxInstructions)
                return Violation::InstructionLimit;

            if ((executedInstructions & 1023u) == 0 &&
                std::chrono::steady_clock::now() - startTime > maxWallTime)
                return Violation::WallTimeLimit;

            return Violation::None;
        }

        Violation accountNativeCall() {
            return ++nativeCallCount > maxNativeCalls ? Violation::NativeCallLimit : Violation::None;
        }

        Violation accountObject() {
            return ++objectCount > maxObjectCount ? Violation::ObjectCountLimit : Violation::None;
        }

        Violation accountArray(std::size_t elementCount) {
            if (elementCount > maxArrayElements)
                return Violation::ArrayElementLimit;

            allocatedBytes += elementCount * 16;   // rough per-element estimate
            return allocatedBytes > maxTotalBytes ? Violation::TotalByteLimit : Violation::None;
        }

        Violation accountString(std::size_t byteCount) {
            if (byteCount > maxStringBytes)
                return Violation::StringByteLimit;

            allocatedBytes += byteCount;
            return allocatedBytes > maxTotalBytes ? Violation::TotalByteLimit : Violation::None;
        }
    };
}
