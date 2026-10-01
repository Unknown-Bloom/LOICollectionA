#pragma once

#include <memory>
#include <cstddef>
#include <chrono>

#include "LOICollectionA/include/client/Plugins/music/audio/SpectrumAnalyzer.h"

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::client::Plugins::music::audio {
    class LoopbackCapture {
    public:
        inline static constexpr auto CaptureStep    = std::chrono::milliseconds(10);
        inline static constexpr auto CaptureTimeout = std::chrono::milliseconds(60);

        inline static constexpr std::size_t FftSize   = 1024;
        inline static constexpr std::size_t BandCount = 24;

        LoopbackCapture();
        ~LoopbackCapture();

        LoopbackCapture(LoopbackCapture const&)            = delete;
        LoopbackCapture(LoopbackCapture&&)                 = delete;
        LoopbackCapture& operator=(LoopbackCapture const&) = delete;
        LoopbackCapture& operator=(LoopbackCapture&&)      = delete;

    public:
        LOICOLLECTION_A_NDAPI bool start();

        LOICOLLECTION_A_API void stop();

        LOICOLLECTION_A_NDAPI bool isRunning() const;

        LOICOLLECTION_A_API void analyze(SpectrumFrame& frame);

        LOICOLLECTION_A_NDAPI const char* getLastError() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
