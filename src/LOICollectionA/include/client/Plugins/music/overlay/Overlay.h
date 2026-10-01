#pragma once

#include <cstdint>
#include <functional>

#include <imgui.h>

#include <ll/api/memory/Hook.h>

struct ID3D11Device;
struct IDXGISwapChain;
struct IDXGISwapChain1;
struct ID3D12CommandQueue;
struct ID3D12CommandList;
struct DXGI_PRESENT_PARAMETERS;

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::client::Plugins::music::overlay {
    using PresentFn             = std::int32_t(__stdcall*)(IDXGISwapChain*, std::uint32_t, std::uint32_t);
    using Present1Fn            = std::int32_t(__stdcall*)(IDXGISwapChain1*, std::uint32_t, std::uint32_t, DXGI_PRESENT_PARAMETERS const*);
    using ResizeBuffersFn       = std::int32_t(__stdcall*)(IDXGISwapChain*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
    using ExecuteCommandListsFn = void(__stdcall*)(ID3D12CommandQueue*, std::uint32_t, ID3D12CommandList* const*);

    using RenderCallback = std::function<void(float deltaTime, float screenWidth, float screenHeight)>;

    struct OverlayTuning {
        float TitleFontSize { 24.0f };
        float ArtistFontSize { 18.0f };

        float MaxFrameDeltaTime { 0.1f };

        std::uint32_t SwapChainPresentIndex { 8 };
        std::uint32_t SwapChainResizeBuffersIndex { 13 };
        std::uint32_t SwapChainPresent1Index { 22 };
        std::uint32_t CommandQueueExecuteCommandListsIndex { 10 };

        ll::memory::HookPriority Priority { ll::memory::HookPriority::Normal };

        bool SuspendThreads { true };

        std::uint64_t ShutdownWaitMs { 50 };
        std::uint64_t CaptureWarmupMs { 60 };
    };

    LOICOLLECTION_A_NDAPI bool isReady();

    LOICOLLECTION_A_API bool applyHooks();

    LOICOLLECTION_A_API void removeHooks();

    LOICOLLECTION_A_API bool patchPresent(void* target);

    LOICOLLECTION_A_API bool patchPresent1(void* target);

    LOICOLLECTION_A_API bool patchResizeBuffers(void* target);

    LOICOLLECTION_A_API bool patchExecuteCommandLists(void* target);

    LOICOLLECTION_A_API std::int32_t __stdcall presentDetour(
        IDXGISwapChain* swapChain,
        std::uint32_t   syncInterval,
        std::uint32_t   flags
    );

    LOICOLLECTION_A_API std::int32_t __stdcall present1Detour(
        IDXGISwapChain1*               swapChain,
        std::uint32_t                  syncInterval,
        std::uint32_t                  flags,
        DXGI_PRESENT_PARAMETERS const* parameters
    );

    LOICOLLECTION_A_API std::int32_t __stdcall resizeBuffersDetour(
        IDXGISwapChain* swapChain,
        std::uint32_t   bufferCount,
        std::uint32_t   width,
        std::uint32_t   height,
        std::uint32_t   format,
        std::uint32_t   swapChainFlags
    );

    LOICOLLECTION_A_API void __stdcall executeCommandListsDetour(
        ID3D12CommandQueue*       queue,
        std::uint32_t             count,
        ID3D12CommandList* const* lists
    );

    LOICOLLECTION_A_NDAPI PresentFn getOriginalPresent();

    LOICOLLECTION_A_NDAPI Present1Fn getOriginalPresent1();

    LOICOLLECTION_A_NDAPI ResizeBuffersFn getOriginalResizeBuffers();

    LOICOLLECTION_A_NDAPI ExecuteCommandListsFn getOriginalExecuteCommandLists();

    LOICOLLECTION_A_API void setRenderCallback(RenderCallback callback);

    LOICOLLECTION_A_NDAPI ImFont* getTitleFont();
    LOICOLLECTION_A_NDAPI ImFont* getArtistFont();

    LOICOLLECTION_A_NDAPI const OverlayTuning& getTuning();

    LOICOLLECTION_A_API void setTuning(const OverlayTuning& tuning);

    LOICOLLECTION_A_NDAPI ID3D11Device* getDevice();
}
