#include <atomic>
#include <filesystem>

#define D3D12_FEATURE_DATA_D3D12_OPTIONS D3D12_FEATURE_DATA_D3D12_OPTIONS_LEGACY
#define D3D12_FEATURE_DATA_ARCHITECTURE  D3D12_FEATURE_DATA_ARCHITECTURE_LEGACY
#define D3D12_RAYTRACING_GEOMETRY_DESC   D3D12_RAYTRACING_GEOMETRY_DESC_LEGACY

#include <windows.h>
#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#undef D3D12_FEATURE_DATA_D3D12_OPTIONS
#undef D3D12_FEATURE_DATA_ARCHITECTURE
#undef D3D12_RAYTRACING_GEOMETRY_DESC

#include <imgui.h>

#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include <ll/api/memory/Hook.h>

#include "LOICollectionA/include/client/Plugins/music/overlay/Overlay.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace LOICollection::client::Plugins::music::overlay {
    namespace {
        struct OverlayHookTargets {
            void* Present {};
            void* Present1 {};
            void* ResizeBuffers {};
            void* ExecuteCommandLists {};
        };

        struct OverlayHookOriginals {
            ll::memory::FuncPtr Present {};
            ll::memory::FuncPtr Present1 {};
            ll::memory::FuncPtr ResizeBuffers {};
            ll::memory::FuncPtr ExecuteCommandLists {};
        };

        OverlayTuning g_tuning;

        OverlayHookTargets   g_targets;
        OverlayHookOriginals g_originals;

        ID3D11Device*       g_device { nullptr };
        ID3D11DeviceContext* g_context { nullptr };
        ID3D11On12Device*   g_d3d11On12 { nullptr };
        ID3D12CommandQueue* g_gameQueue { nullptr };

        HWND    g_window { nullptr };
        WNDPROC g_originalWndProc { nullptr };
        bool    g_imguiReady { false };

        std::atomic_bool g_hooksInstalled { false };
        std::atomic_bool g_initializing { false };
        std::atomic_bool g_shuttingDown { false };

        std::atomic<ULONGLONG> g_lastFrameTick { 0 };

        ImFont* g_titleFont { nullptr };
        ImFont* g_artistFont { nullptr };

        RenderCallback g_renderCallback;

        void applyFonts(ImGuiIO& io) {
            struct FontSource {
                char const*    path;
                ImWchar const* ranges;
            };

            FontSource const sources[] = {
                { "C:\\Windows\\Fonts\\segoeui.ttf", nullptr                              },
                { "C:\\Windows\\Fonts\\msyh.ttc",    io.Fonts->GetGlyphRangesChineseFull() },
                { "C:\\Windows\\Fonts\\meiryo.ttc",  io.Fonts->GetGlyphRangesJapanese()    },
                { "C:\\Windows\\Fonts\\malgun.ttf",  io.Fonts->GetGlyphRangesKorean()      },
            };

            auto build = [&sources, &io](float size) -> ImFont* {
                ImFontConfig config;
                config.OversampleH = 1;
                config.OversampleV = 1;

                ImFont* primary { nullptr };

                for (auto const& source : sources) {
                    if (!std::filesystem::exists(source.path))
                        continue;

                    ImFont* font = io.Fonts->AddFontFromFileTTF(source.path, size, &config, source.ranges);

                    if (font == nullptr)
                        continue;

                    if (primary == nullptr)
                        primary = font;

                    config.MergeMode = true;
                }

                if (primary == nullptr)
                    primary = io.Fonts->AddFontDefault();

                return primary;
            };

            g_titleFont  = build(g_tuning.TitleFontSize);
            g_artistFont = build(g_tuning.ArtistFontSize);
        }

        LRESULT __stdcall wndProcHook(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
            if (g_imguiReady && !g_shuttingDown.load(std::memory_order_acquire))
                ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam);

            return ::CallWindowProcW(g_originalWndProc, hWnd, message, wParam, lParam);
        }

        void initializeOnPresent(IDXGISwapChain* swapChain) {
            if (g_initializing.exchange(true))
                return;

            if (FAILED(swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_device)))) {
                if (g_gameQueue != nullptr) {
                    ID3D12Device* d3d12Device { nullptr };

                    if (SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&d3d12Device)))) {
                        ::D3D11On12CreateDevice(
                            d3d12Device,
                            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                            nullptr,
                            0,
                            reinterpret_cast<IUnknown**>(&g_gameQueue),
                            1,
                            0,
                            &g_device,
                            &g_context,
                            nullptr
                        );

                        if (g_device != nullptr)
                            g_device->QueryInterface(__uuidof(ID3D11On12Device), reinterpret_cast<void**>(&g_d3d11On12));

                        d3d12Device->Release();
                    }
                }
            } else {
                g_device->GetImmediateContext(&g_context);
            }

            if (g_device == nullptr) {
                g_initializing.store(false);

                return;
            }

            DXGI_SWAP_CHAIN_DESC description {};
            swapChain->GetDesc(&description);

            g_window = description.OutputWindow != nullptr ? description.OutputWindow : ::FindWindowW(L"Minecraft", nullptr);

            if (g_window != nullptr)
                g_originalWndProc = reinterpret_cast<WNDPROC>(
                    ::SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wndProcHook))
                );

            ImGui::CreateContext();

            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;

            applyFonts(io);

            ImGui_ImplWin32_Init(g_window);
            ImGui_ImplDX11_Init(g_device, g_context);

            g_imguiReady = true;
        }

        void drawFrame(ID3D11RenderTargetView* renderTarget, float deltaTime, float width, float height) {
            g_context->OMSetRenderTargets(1, &renderTarget, nullptr);

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            if (g_renderCallback)
                g_renderCallback(deltaTime, width, height);

            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

            ID3D11RenderTargetView* nullTarget { nullptr };
            g_context->OMSetRenderTargets(1, &nullTarget, nullptr);
        }

        void renderImGui(IDXGISwapChain* swapChain) {
            if (!g_imguiReady || g_shuttingDown.load(std::memory_order_acquire) || !g_renderCallback)
                return;

            DXGI_SWAP_CHAIN_DESC description {};
            swapChain->GetDesc(&description);

            float width  = static_cast<float>(description.BufferDesc.Width);
            float height = static_cast<float>(description.BufferDesc.Height);

            if (width <= 0.0f || height <= 0.0f)
                return;

            ULONGLONG now       = ::GetTickCount64();
            ULONGLONG previous  = g_lastFrameTick.exchange(now);
            float     deltaTime = previous == 0 ? 0.0f : static_cast<float>(now - previous) / 1000.0f;

            if (deltaTime > static_cast<float>(g_tuning.MaxFrameDeltaTime))
                deltaTime = static_cast<float>(g_tuning.MaxFrameDeltaTime);

            if (g_d3d11On12 != nullptr) {
                UINT bufferIndex = 0;

                IDXGISwapChain3* swapChain3 { nullptr };

                if (SUCCEEDED(swapChain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&swapChain3)))) {
                    bufferIndex = swapChain3->GetCurrentBackBufferIndex();
                    swapChain3->Release();
                }

                ID3D12Resource* backBuffer { nullptr };

                if (FAILED(swapChain->GetBuffer(bufferIndex, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&backBuffer))))
                    return;

                ID3D11Resource*    wrapped { nullptr };
                D3D11_RESOURCE_FLAGS flags { D3D11_BIND_RENDER_TARGET };

                if (SUCCEEDED(g_d3d11On12->CreateWrappedResource(
                        backBuffer,
                        &flags,
                        D3D12_RESOURCE_STATE_PRESENT,
                        D3D12_RESOURCE_STATE_PRESENT,
                        __uuidof(ID3D11Resource),
                        reinterpret_cast<void**>(&wrapped)
                    ))) {
                    ID3D11RenderTargetView* renderTarget { nullptr };
                    g_device->CreateRenderTargetView(wrapped, nullptr, &renderTarget);

                    g_d3d11On12->AcquireWrappedResources(&wrapped, 1);

                    if (renderTarget != nullptr)
                        drawFrame(renderTarget, deltaTime, width, height);

                    g_d3d11On12->ReleaseWrappedResources(&wrapped, 1);
                    wrapped->Release();

                    if (renderTarget != nullptr)
                        renderTarget->Release();

                    g_context->Flush();
                }

                backBuffer->Release();

                return;
            }

            ID3D11Texture2D* backBuffer { nullptr };

            if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer))))
                return;

            ID3D11RenderTargetView* renderTarget { nullptr };
            g_device->CreateRenderTargetView(backBuffer, nullptr, &renderTarget);
            backBuffer->Release();

            if (renderTarget != nullptr) {
                drawFrame(renderTarget, deltaTime, width, height);
                renderTarget->Release();
            }
        }

        void releaseResources() {
            if (g_imguiReady) {
                ImGui_ImplDX11_Shutdown();
                ImGui_ImplWin32_Shutdown();
                ImGui::DestroyContext();

                g_imguiReady = false;
            }

            if (g_context != nullptr) {
                ID3D11RenderTargetView* nullTarget { nullptr };
                g_context->OMSetRenderTargets(1, &nullTarget, nullptr);
                g_context->ClearState();
                g_context->Flush();
            }

            if (g_d3d11On12 != nullptr) {
                g_d3d11On12->Release();
                g_d3d11On12 = nullptr;
            }

            if (g_context != nullptr) {
                g_context->Release();
                g_context = nullptr;
            }

            if (g_device != nullptr) {
                g_device->Release();
                g_device = nullptr;
            }

            if (g_gameQueue != nullptr) {
                g_gameQueue->Release();
                g_gameQueue = nullptr;
            }
        }

        bool patchHook(void*& target, void* address, ll::memory::FuncPtr detour, ll::memory::FuncPtr* original) {
            if (target != nullptr)
                return true;

            if (ll::memory::hook(address, detour, original, g_tuning.Priority, g_tuning.SuspendThreads) == 0)
                return false;

            target = address;

            return true;
        }
    }

    std::int32_t __stdcall presentDetour(IDXGISwapChain* swapChain, std::uint32_t syncInterval, std::uint32_t flags) {
        auto original = reinterpret_cast<PresentFn>(g_originals.Present);

        if (g_shuttingDown.load(std::memory_order_acquire))
            return original(swapChain, syncInterval, flags);

        if (!g_imguiReady)
            initializeOnPresent(swapChain);
        else
            renderImGui(swapChain);

        return original(swapChain, syncInterval, flags);
    }

    std::int32_t __stdcall present1Detour(
        IDXGISwapChain1*               swapChain,
        std::uint32_t                  syncInterval,
        std::uint32_t                  flags,
        DXGI_PRESENT_PARAMETERS const* parameters
    ) {
        auto original = reinterpret_cast<Present1Fn>(g_originals.Present1);

        if (g_shuttingDown.load(std::memory_order_acquire))
            return original(swapChain, syncInterval, flags, parameters);

        if (!g_imguiReady)
            initializeOnPresent(swapChain);
        else
            renderImGui(swapChain);

        return original(swapChain, syncInterval, flags, parameters);
    }

    std::int32_t __stdcall resizeBuffersDetour(
        IDXGISwapChain* swapChain,
        std::uint32_t   bufferCount,
        std::uint32_t   width,
        std::uint32_t   height,
        std::uint32_t   format,
        std::uint32_t   swapChainFlags
    ) {
        auto original = reinterpret_cast<ResizeBuffersFn>(g_originals.ResizeBuffers);

        if (g_imguiReady) {
            ImGui_ImplDX11_InvalidateDeviceObjects();

            std::int32_t result = original(
                swapChain,
                bufferCount,
                width,
                height,
                static_cast<DXGI_FORMAT>(format),
                swapChainFlags
            );

            ImGui_ImplDX11_CreateDeviceObjects();

            return result;
        }

        return original(swapChain, bufferCount, width, height, static_cast<DXGI_FORMAT>(format), swapChainFlags);
    }

    void __stdcall executeCommandListsDetour(
        ID3D12CommandQueue*       queue,
        std::uint32_t             count,
        ID3D12CommandList* const* lists
    ) {
        if (g_gameQueue == nullptr && queue != nullptr) {
            D3D12_COMMAND_QUEUE_DESC description = queue->GetDesc();

            if (description.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
                g_gameQueue = queue;
                g_gameQueue->AddRef();
            }
        }

        reinterpret_cast<ExecuteCommandListsFn>(g_originals.ExecuteCommandLists)(queue, count, lists);
    }

    ID3D11Device* getDevice() {
        return g_device;
    }

    bool isReady() {
        return g_imguiReady;
    }

    const OverlayTuning& getTuning() {
        return g_tuning;
    }

    void setTuning(const OverlayTuning& tuning) {
        g_tuning = tuning;
    }

    bool patchPresent(void* target) {
        return target == nullptr
            || patchHook(g_targets.Present, target, ll::memory::toFuncPtr(presentDetour), &g_originals.Present);
    }

    bool patchPresent1(void* target) {
        return target == nullptr
            || patchHook(g_targets.Present1, target, ll::memory::toFuncPtr(present1Detour), &g_originals.Present1);
    }

    bool patchResizeBuffers(void* target) {
        return target == nullptr
            || patchHook(g_targets.ResizeBuffers, target, ll::memory::toFuncPtr(resizeBuffersDetour), &g_originals.ResizeBuffers);
    }

    bool patchExecuteCommandLists(void* target) {
        return target == nullptr
            || patchHook(
                g_targets.ExecuteCommandLists,
                target,
                ll::memory::toFuncPtr(executeCommandListsDetour),
                &g_originals.ExecuteCommandLists
            );
    }

    void removePatches() {
        if (g_targets.Present != nullptr) {
            ll::memory::unhook(g_targets.Present, ll::memory::toFuncPtr(presentDetour), false);
            g_targets.Present   = nullptr;
            g_originals.Present = nullptr;
        }

        if (g_targets.Present1 != nullptr) {
            ll::memory::unhook(g_targets.Present1, ll::memory::toFuncPtr(present1Detour), false);
            g_targets.Present1   = nullptr;
            g_originals.Present1 = nullptr;
        }

        if (g_targets.ResizeBuffers != nullptr) {
            ll::memory::unhook(g_targets.ResizeBuffers, ll::memory::toFuncPtr(resizeBuffersDetour), false);
            g_targets.ResizeBuffers   = nullptr;
            g_originals.ResizeBuffers = nullptr;
        }

        if (g_targets.ExecuteCommandLists != nullptr) {
            ll::memory::unhook(g_targets.ExecuteCommandLists, ll::memory::toFuncPtr(executeCommandListsDetour), false);
            g_targets.ExecuteCommandLists   = nullptr;
            g_originals.ExecuteCommandLists = nullptr;
        }
    }

    bool applyHooks() {
        if (g_hooksInstalled.load(std::memory_order_acquire))
            return true;

        HWND window = ::FindWindowW(L"Minecraft", nullptr);
        if (window == nullptr)
            window = ::GetForegroundWindow();

        if (window == nullptr)
            return false;

        DXGI_SWAP_CHAIN_DESC description {};
        description.BufferCount       = 1;
        description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.OutputWindow      = window;
        description.SampleDesc.Count  = 1;
        description.Windowed          = TRUE;
        description.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

        ID3D11Device*        dummyDevice { nullptr };
        IDXGISwapChain*      dummySwapChain { nullptr };
        ID3D11DeviceContext* dummyContext { nullptr };

        HRESULT result = ::D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            &featureLevel,
            1,
            D3D11_SDK_VERSION,
            &description,
            &dummySwapChain,
            &dummyDevice,
            nullptr,
            &dummyContext
        );

        if (SUCCEEDED(result) && dummySwapChain != nullptr) {
            void** vtable = *reinterpret_cast<void***>(dummySwapChain);

            patchPresent(vtable[g_tuning.SwapChainPresentIndex]);
            patchResizeBuffers(vtable[g_tuning.SwapChainResizeBuffersIndex]);

            IDXGISwapChain1* dummySwapChain1 { nullptr };

            if (SUCCEEDED(dummySwapChain->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&dummySwapChain1)))) {
                patchPresent1((*reinterpret_cast<void***>(dummySwapChain1))[g_tuning.SwapChainPresent1Index]);

                dummySwapChain1->Release();
            }

            dummySwapChain->Release();
            dummyDevice->Release();
            dummyContext->Release();
        }

        ID3D12Device* d3d12Device { nullptr };

        if (SUCCEEDED(::D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&d3d12Device)))) {
            D3D12_COMMAND_QUEUE_DESC queueDescription {};
            queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

            ID3D12CommandQueue* dummyQueue { nullptr };

            if (SUCCEEDED(d3d12Device->CreateCommandQueue(&queueDescription, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&dummyQueue)))) {
                patchExecuteCommandLists((*reinterpret_cast<void***>(dummyQueue))[g_tuning.CommandQueueExecuteCommandListsIndex]);

                dummyQueue->Release();
            }

            d3d12Device->Release();
        }

        g_hooksInstalled.store(true, std::memory_order_release);

        return true;
    }

    void removeHooks() {
        if (!g_hooksInstalled.exchange(false, std::memory_order_acq_rel))
            return;

        g_shuttingDown.store(true, std::memory_order_release);

        ::Sleep(static_cast<DWORD>(g_tuning.ShutdownWaitMs));

        if (g_originalWndProc != nullptr && g_window != nullptr) {
            ::SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_originalWndProc));
            g_originalWndProc = nullptr;
        }

        releaseResources();

        removePatches();

        g_shuttingDown.store(false, std::memory_order_release);
        g_lastFrameTick.store(0, std::memory_order_release);
    }

    PresentFn getOriginalPresent() {
        return reinterpret_cast<PresentFn>(g_originals.Present);
    }

    Present1Fn getOriginalPresent1() {
        return reinterpret_cast<Present1Fn>(g_originals.Present1);
    }

    ResizeBuffersFn getOriginalResizeBuffers() {
        return reinterpret_cast<ResizeBuffersFn>(g_originals.ResizeBuffers);
    }

    ExecuteCommandListsFn getOriginalExecuteCommandLists() {
        return reinterpret_cast<ExecuteCommandListsFn>(g_originals.ExecuteCommandLists);
    }

    void setRenderCallback(RenderCallback callback) {
        g_renderCallback = std::move(callback);
    }

    ImFont* getTitleFont() {
        return g_titleFont;
    }

    ImFont* getArtistFont() {
        return g_artistFont;
    }
}
