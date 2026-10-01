#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <cstdint>

#include <objbase.h>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include "LOICollectionA/include/client/Plugins/music/audio/LoopbackCapture.h"

namespace LOICollection::client::Plugins::music::audio {
    namespace {
        std::string formatErrorMessage(HRESULT result) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "HRESULT 0x%08lX", static_cast<unsigned long>(result));

            return buffer;
        }
    }

    struct LoopbackCapture::Impl {
        std::thread      Worker;
        std::atomic_bool Running { false };

        std::mutex         SamplesMutex;
        std::vector<float> Samples;

        SpectrumAnalyzer Analyzer { FftSize, BandCount };

        std::string LastError;
    };

    LoopbackCapture::LoopbackCapture() : mImpl(std::make_unique<Impl>()) {}
    LoopbackCapture::~LoopbackCapture() {
        this->stop();
    }

    bool LoopbackCapture::isRunning() const {
        return this->mImpl->Running.load(std::memory_order_acquire);
    }

    const char* LoopbackCapture::getLastError() const {
        return this->mImpl->LastError.c_str();
    }

    bool LoopbackCapture::start() {
        if (this->mImpl->Running.load(std::memory_order_acquire))
            return true;

        this->mImpl->LastError.clear();
        this->mImpl->Running.store(true, std::memory_order_release);

        this->mImpl->Worker = std::thread([this]() -> void {
            HRESULT comResult = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

            IMMDeviceEnumerator* enumerator { nullptr };
            IMMDevice*           device { nullptr };
            IAudioClient*        audioClient { nullptr };
            IAudioCaptureClient* captureClient { nullptr };

            auto cleanup = [&]() -> void {
                if (captureClient != nullptr)
                    captureClient->Release();

                if (audioClient != nullptr)
                    audioClient->Release();

                if (device != nullptr)
                    device->Release();

                if (enumerator != nullptr)
                    enumerator->Release();

                if (SUCCEEDED(comResult))
                    ::CoUninitialize();
            };

            auto fail = [&](const char* stage, HRESULT result) -> void {
                this->mImpl->LastError = std::string(stage) + " failed: " + formatErrorMessage(result);

                this->mImpl->Running.store(false, std::memory_order_release);
            };

            HRESULT result = ::CoCreateInstance(
                __uuidof(MMDeviceEnumerator),
                nullptr,
                CLSCTX_ALL,
                __uuidof(IMMDeviceEnumerator),
                reinterpret_cast<void**>(&enumerator)
            );

            if (FAILED(result)) {
                fail("CoCreateInstance(MMDeviceEnumerator)", result);
                cleanup();

                return;
            }

            result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);

            if (FAILED(result)) {
                fail("GetDefaultAudioEndpoint", result);
                cleanup();

                return;
            }

            result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&audioClient));

            if (FAILED(result)) {
                fail("Activate(IAudioClient)", result);
                cleanup();

                return;
            }

            WAVEFORMATEX* mixFormat { nullptr };

            result = audioClient->GetMixFormat(&mixFormat);

            if (FAILED(result) || mixFormat == nullptr) {
                fail("GetMixFormat", result);
                cleanup();

                return;
            }

            const WORD  channels = mixFormat->nChannels;
            const float rate     = static_cast<float>(mixFormat->nSamplesPerSec);

            WAVEFORMATEXTENSIBLE converted {};
            converted.Format.wFormatTag           = WAVE_FORMAT_EXTENSIBLE;
            converted.Format.nChannels            = 2;
            converted.Format.nSamplesPerSec       = mixFormat->nSamplesPerSec;
            converted.Format.wBitsPerSample       = 32;
            converted.Format.nBlockAlign          = static_cast<WORD>(converted.Format.nChannels * sizeof(float));
            converted.Format.nAvgBytesPerSec      = converted.Format.nSamplesPerSec * converted.Format.nBlockAlign;
            converted.Format.cbSize               = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
            converted.Samples.wValidBitsPerSample = 32;
            converted.dwChannelMask               = 3;
            converted.SubFormat                   = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

            const DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
                              | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

            result = audioClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                flags,
                200000,
                0,
                reinterpret_cast<WAVEFORMATEX*>(&converted),
                nullptr
            );

            if (FAILED(result)) {
                result = audioClient->Initialize(
                    AUDCLNT_SHAREMODE_SHARED,
                    AUDCLNT_STREAMFLAGS_LOOPBACK,
                    200000,
                    0,
                    mixFormat,
                    nullptr
                );
            }

            if (FAILED(result)) {
                fail("IAudioClient::Initialize", result);
                ::CoTaskMemFree(mixFormat);
                cleanup();

                return;
            }

            result = audioClient->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&captureClient));

            if (FAILED(result)) {
                fail("GetService(IAudioCaptureClient)", result);
                ::CoTaskMemFree(mixFormat);
                cleanup();

                return;
            }

            result = audioClient->Start();

            if (FAILED(result)) {
                fail("IAudioClient::Start", result);
                ::CoTaskMemFree(mixFormat);
                cleanup();

                return;
            }

            this->mImpl->Analyzer.reset(rate);

            std::vector<float> mono;

            while (this->mImpl->Running.load(std::memory_order_acquire)) {
                UINT32 packetLength = 0;

                result = captureClient->GetNextPacketSize(&packetLength);

                if (FAILED(result)) {
                    fail("GetNextPacketSize", result);

                    break;
                }

                if (packetLength == 0) {
                    std::this_thread::sleep_for(CaptureStep);

                    continue;
                }

                BYTE*  data { nullptr };
                UINT32 frameCount { 0 };
                DWORD  bufferFlags { 0 };

                result = captureClient->GetBuffer(&data, &frameCount, &bufferFlags, nullptr, nullptr);

                if (FAILED(result)) {
                    fail("GetBuffer", result);

                    break;
                }

                mono.clear();
                mono.reserve(frameCount);

                if (frameCount > 0 && data != nullptr && (bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) == 0) {
                    const auto* samples = reinterpret_cast<const float*>(data);

                    for (UINT32 frame = 0; frame < frameCount; ++frame) {
                        float sum = 0.0f;

                        for (WORD channel = 0; channel < channels; ++channel)
                            sum += samples[static_cast<std::size_t>(frame) * channels + channel];

                        mono.push_back(sum / static_cast<float>(channels));
                    }
                } else {
                    mono.assign(frameCount, 0.0f);
                }

                captureClient->ReleaseBuffer(frameCount);

                if (!mono.empty()) {
                    std::scoped_lock lock(this->mImpl->SamplesMutex);

                    this->mImpl->Analyzer.push(mono.data(), mono.size());
                }

                std::this_thread::sleep_for(CaptureStep);
            }

            audioClient->Stop();

            ::CoTaskMemFree(mixFormat);

            this->mImpl->Running.store(false, std::memory_order_release);

            cleanup();
        });

        std::this_thread::sleep_for(CaptureTimeout);

        return true;
    }

    void LoopbackCapture::stop() {
        if (!this->mImpl->Running.exchange(false, std::memory_order_acq_rel))
            return;

        if (this->mImpl->Worker.joinable())
            this->mImpl->Worker.join();
    }

    void LoopbackCapture::analyze(SpectrumFrame& frame) {
        std::scoped_lock lock(this->mImpl->SamplesMutex);

        this->mImpl->Analyzer.analyze(frame);
    }
}
