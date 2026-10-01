#pragma once

#include <memory>
#include <cstddef>
#include <vector>

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::client::Plugins::music::audio {
    struct SpectrumFrame {
        std::vector<float> Bands;

        std::vector<float> Waveform;

        float Level { 0.0f };

        [[nodiscard]] bool empty() const {
            return this->Bands.empty();
        }
    };

    struct SpectrumTuning {
        float MinDecibels { -72.0f };
        float MaxDecibels { -6.0f };

        float MinFrequency { 40.0f };
        float MaxFrequency { 14000.0f };

        float HannCoherentGain { 0.5f };

        float AttackRatio { 1.0f };
        float ReleaseRatio { 0.82f };
        float NewValueRatio { 0.18f };

        float WaveformHoldRatio { 0.7f };
        float WaveformNewRatio { 0.3f };
    };

    class SpectrumAnalyzer {
    public:
        LOICOLLECTION_A_API explicit SpectrumAnalyzer(
            std::size_t fftSize = 1024,
            std::size_t bandCount = 24,
            SpectrumTuning tuning = {}
        );
        LOICOLLECTION_A_API ~SpectrumAnalyzer();

        SpectrumAnalyzer(SpectrumAnalyzer const&)            = delete;
        SpectrumAnalyzer(SpectrumAnalyzer&&)                 = delete;
        SpectrumAnalyzer& operator=(SpectrumAnalyzer const&) = delete;
        SpectrumAnalyzer& operator=(SpectrumAnalyzer&&)      = delete;

    public:
        LOICOLLECTION_A_API void push(const float* samples, std::size_t count);

        LOICOLLECTION_A_API void analyze(SpectrumFrame& frame);

        LOICOLLECTION_A_API void reset(float sampleRate);

        LOICOLLECTION_A_NDAPI bool isEmpty() const;

        LOICOLLECTION_A_NDAPI const SpectrumTuning& getTuning() const;

        LOICOLLECTION_A_API void setTuning(const SpectrumTuning& tuning);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
