#include <cmath>
#include <memory>
#include <vector>
#include <cstddef>
#include <algorithm>
#include <numbers>

#include "LOICollectionA/include/client/Plugins/music/audio/SpectrumAnalyzer.h"

namespace LOICollection::client::Plugins::music::audio {
    struct SpectrumAnalyzer::Impl {
        SpectrumTuning Tuning;

        std::size_t FftSize { 1024 };
        std::size_t BandCount { 24 };

        float SampleRate { 48000.0f };

        std::vector<float> Ring;
        std::size_t        WriteIndex { 0 };
        bool               Filled { false };

        std::vector<float> Window;
        std::vector<float> Real;
        std::vector<float> Imaginary;

        float AmplitudeScale { 1.0f };

        std::vector<std::size_t> BandEdges;

        std::vector<float> Magnitudes;

        std::vector<float> TwiddleReal;
        std::vector<float> TwiddleImag;

        std::vector<float> SmoothedBands;
        std::vector<float> SmoothedWave;

        bool Ready { false };

        Impl(std::size_t fftSize, std::size_t bandCount, SpectrumTuning tuning)
        : Tuning(tuning),
          FftSize(fftSize),
          BandCount(bandCount) {
            this->Ring.resize(fftSize, 0.0f);
            this->Real.resize(fftSize, 0.0f);
            this->Imaginary.resize(fftSize, 0.0f);
            this->Window.resize(fftSize, 1.0f);
            this->Magnitudes.resize(fftSize / 2, 0.0f);
            this->SmoothedBands.resize(bandCount, 0.0f);
            this->SmoothedWave.resize(bandCount, 0.0f);

            for (std::size_t index = 0; index < fftSize; ++index) {
                this->Window[index] = 0.5f
                                    * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(index)
                                                      / static_cast<float>(fftSize - 1)));
            }

            this->TwiddleReal.resize(fftSize / 2);
            this->TwiddleImag.resize(fftSize / 2);

            for (std::size_t index = 0; index < fftSize / 2; ++index) {
                float angle = -2.0f * std::numbers::pi_v<float> * static_cast<float>(index) / static_cast<float>(fftSize);

                this->TwiddleReal[index] = std::cos(angle);
                this->TwiddleImag[index] = std::sin(angle);
            }

            this->rebuildAmplitudeScale();
            this->rebuildBands();
        }

        void rebuildAmplitudeScale() {
            this->AmplitudeScale = 2.0f / (static_cast<float>(this->FftSize) * this->Tuning.HannCoherentGain);
        }

        void rebuildBands() {
            this->BandEdges.clear();
            this->BandEdges.reserve(this->BandCount + 1);

            float nyquist = this->SampleRate * 0.5f;

            for (std::size_t index = 0; index <= this->BandCount; ++index) {
                float ratio = static_cast<float>(index) / static_cast<float>(this->BandCount);

                float frequency = this->Tuning.MinFrequency
                                * std::pow(this->Tuning.MaxFrequency / this->Tuning.MinFrequency, ratio);
                float bin = frequency / nyquist * static_cast<float>(this->FftSize / 2);

                this->BandEdges.push_back(static_cast<std::size_t>(std::clamp(
                    bin,
                    1.0f,
                    static_cast<float>(this->FftSize / 2 - 1)
                )));
            }
        }

        float toNormalized(float magnitude) const {
            float decibels = 20.0f * std::log10(std::max(magnitude, 1e-9f));

            return std::clamp(
                (decibels - this->Tuning.MinDecibels) / (this->Tuning.MaxDecibels - this->Tuning.MinDecibels),
                0.0f,
                1.0f
            );
        }

        void transform() {
            std::size_t n = this->FftSize;

            for (std::size_t i = 1, j = 0; i < n; ++i) {
                std::size_t bit = n >> 1;

                for (; (j & bit) != 0; bit >>= 1)
                    j ^= bit;

                j ^= bit;

                if (i < j) {
                    std::swap(this->Real[i], this->Real[j]);
                    std::swap(this->Imaginary[i], this->Imaginary[j]);
                }
            }

            for (std::size_t length = 2; length <= n; length <<= 1) {
                std::size_t half = length / 2;
                std::size_t step = n / length;

                for (std::size_t offset = 0; offset < n; offset += length) {
                    for (std::size_t k = 0; k < half; ++k) {
                        std::size_t twiddle = k * step;

                        float currentReal = this->TwiddleReal[twiddle];
                        float currentImag = this->TwiddleImag[twiddle];

                        std::size_t even = offset + k;
                        std::size_t odd  = even + half;

                        float oddReal = this->Real[odd] * currentReal - this->Imaginary[odd] * currentImag;
                        float oddImag = this->Real[odd] * currentImag + this->Imaginary[odd] * currentReal;

                        this->Real[odd]      = this->Real[even] - oddReal;
                        this->Imaginary[odd] = this->Imaginary[even] - oddImag;

                        this->Real[even] += oddReal;
                        this->Imaginary[even] += oddImag;
                    }
                }
            }
        }
    };

    SpectrumAnalyzer::SpectrumAnalyzer(std::size_t fftSize, std::size_t bandCount, SpectrumTuning tuning)
    : mImpl(std::make_unique<Impl>(fftSize, bandCount, tuning)) {}

    SpectrumAnalyzer::~SpectrumAnalyzer() = default;

    bool SpectrumAnalyzer::isEmpty() const {
        return !this->mImpl->Ready || !this->mImpl->Filled;
    }

    const SpectrumTuning& SpectrumAnalyzer::getTuning() const {
        return this->mImpl->Tuning;
    }

    void SpectrumAnalyzer::setTuning(const SpectrumTuning& tuning) {
        this->mImpl->Tuning = tuning;

        this->mImpl->rebuildAmplitudeScale();
        this->mImpl->rebuildBands();
    }

    void SpectrumAnalyzer::reset(float sampleRate) {
        if (sampleRate > 0.0f)
            this->mImpl->SampleRate = sampleRate;

        std::fill(this->mImpl->Ring.begin(), this->mImpl->Ring.end(), 0.0f);
        std::fill(this->mImpl->SmoothedBands.begin(), this->mImpl->SmoothedBands.end(), 0.0f);
        std::fill(this->mImpl->SmoothedWave.begin(), this->mImpl->SmoothedWave.end(), 0.0f);

        this->mImpl->WriteIndex = 0;
        this->mImpl->Filled     = false;
        this->mImpl->Ready      = true;

        this->mImpl->rebuildBands();
    }

    void SpectrumAnalyzer::push(const float* samples, std::size_t count) {
        if (samples == nullptr || count == 0)
            return;

        Impl& impl = *this->mImpl;

        if (!impl.Ready)
            this->reset(impl.SampleRate);

        for (std::size_t index = 0; index < count; ++index) {
            impl.Ring[impl.WriteIndex] = samples[index];
            impl.WriteIndex            = (impl.WriteIndex + 1) % impl.FftSize;

            if (impl.WriteIndex == 0)
                impl.Filled = true;
        }
    }

    void SpectrumAnalyzer::analyze(SpectrumFrame& frame) {
        Impl& impl = *this->mImpl;

        if (frame.Bands.size() != impl.BandCount)
            frame.Bands.resize(impl.BandCount, 0.0f);

        if (frame.Waveform.size() != impl.BandCount)
            frame.Waveform.resize(impl.BandCount, 0.0f);

        if (!impl.Filled)
            return;

        for (std::size_t index = 0; index < impl.FftSize; ++index) {
            std::size_t source = (impl.WriteIndex + index) % impl.FftSize;

            impl.Real[index]      = impl.Ring[source] * impl.Window[index];
            impl.Imaginary[index] = 0.0f;
        }

        impl.transform();

        const std::size_t half = impl.FftSize / 2;

        for (std::size_t bin = 0; bin < half; ++bin) {
            float real = impl.Real[bin];
            float imag = impl.Imaginary[bin];

            impl.Magnitudes[bin] = std::sqrt(real * real + imag * imag);
        }

        float peak = 0.0f;

        for (std::size_t band = 0; band < impl.BandCount; ++band) {
            std::size_t begin = std::min(impl.BandEdges[band], half - 1);
            std::size_t end   = std::min(std::max(impl.BandEdges[band + 1], begin + 1), half);

            float magnitude = 0.0f;

            for (std::size_t bin = begin; bin < end; ++bin)
                magnitude = std::max(magnitude, impl.Magnitudes[bin]);

            float value = impl.toNormalized(magnitude * impl.AmplitudeScale);

            float previous = impl.SmoothedBands[band];
            float smoothed = value > previous
                           ? value * impl.Tuning.AttackRatio
                           : previous * impl.Tuning.ReleaseRatio + value * impl.Tuning.NewValueRatio;

            impl.SmoothedBands[band] = smoothed;
            frame.Bands[band]        = smoothed;

            peak = std::max(peak, smoothed);
        }

        for (std::size_t band = 0; band < impl.BandCount; ++band) {
            float left  = frame.Bands[band > 0 ? band - 1 : band];
            float right = frame.Bands[band + 1 < impl.BandCount ? band + 1 : band];

            float average = (left + frame.Bands[band] + right) / 3.0f;

            impl.SmoothedWave[band] = impl.SmoothedWave[band] * impl.Tuning.WaveformHoldRatio
                                    + average * impl.Tuning.WaveformNewRatio;
            frame.Waveform[band]    = impl.SmoothedWave[band];
        }

        frame.Level = peak;
    }
}
