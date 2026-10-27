#pragma once

#include <chrono>
#include <cstddef>

namespace LOICollection::frontend::sandbox {
    struct SandboxBudget {
        std::size_t maxInstructions = 1'000'000;
        std::chrono::milliseconds maxWallTime{ 1000 };
        std::size_t maxFrames = 256;
        std::size_t maxNativeCalls = 100'000;
        std::size_t maxObjectCount = 100'000;
        std::size_t maxArrayElements = 100'000;
        std::size_t maxStringBytes = 1 << 20;
        std::size_t maxTotalBytes = 64 << 20;

        std::chrono::steady_clock::duration accumulatedWall{};
        std::chrono::steady_clock::time_point wallAnchor{};
        bool wallRunning = false;
        std::size_t executedInstructions = 0;
        std::size_t nativeCallCount = 0;
        std::size_t objectCount = 0;
        std::size_t allocatedBytes = 0;
        std::size_t activeFrames = 0;

        void reset() {
            accumulatedWall = std::chrono::steady_clock::duration{};
            wallRunning = false;
            executedInstructions = 0;
            nativeCallCount = 0;
            objectCount = 0;
            allocatedBytes = 0;
            activeFrames = 0;
        }

        void beginWall() {
            wallAnchor = std::chrono::steady_clock::now();
            wallRunning = true;
        }

        void endWall() {
            if (wallRunning) {
                accumulatedWall += std::chrono::steady_clock::now() - wallAnchor;
                wallRunning = false;
            }
        }

        std::chrono::steady_clock::duration elapsedWall() const {
            std::chrono::steady_clock::duration e = accumulatedWall;
            if (wallRunning)
                e += std::chrono::steady_clock::now() - wallAnchor;
            return e;
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
            FrameLimit,
        };

        Violation tickInstruction() {
            if (++executedInstructions > maxInstructions)
                return Violation::InstructionLimit;

            if ((executedInstructions & 1023u) == 0 && elapsedWall() > maxWallTime)
                return Violation::WallTimeLimit;

            return Violation::None;
        }

        void accountWall(std::chrono::steady_clock::duration elapsed) {
            accumulatedWall += elapsed;
        }

        Violation accountNativeCall() {
            return ++nativeCallCount > maxNativeCalls ? Violation::NativeCallLimit : Violation::None;
        }

        Violation accountObject() {
            return ++objectCount > maxObjectCount ? Violation::ObjectCountLimit : Violation::None;
        }

        void releaseObject() {
            if (objectCount > 0)
                --objectCount;
        }

        Violation accountArray(std::size_t elementCount) {
            if (elementCount > maxArrayElements)
                return Violation::ArrayElementLimit;

            allocatedBytes += elementCount * 16;
            return allocatedBytes > maxTotalBytes ? Violation::TotalByteLimit : Violation::None;
        }

        void releaseBytes(std::size_t bytes) {
            allocatedBytes = (bytes >= allocatedBytes) ? std::size_t{0} : (allocatedBytes - bytes);
        }

        Violation accountString(std::size_t byteCount) {
            return byteCount > maxStringBytes ? Violation::StringByteLimit : Violation::None;
        }

        Violation accountBytes(std::size_t byteCount) {
            allocatedBytes += byteCount;
            return allocatedBytes > maxTotalBytes ? Violation::TotalByteLimit : Violation::None;
        }
    };
}
