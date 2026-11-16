#include "../DXGI/SwapchainHook.hpp"
#include <winrt/base.h>
#include "../../../../../GUI/D2D.hpp"
#include "../../../../../Events/Render/RenderEvent.hpp"
#include "../../../../../Events/Render/RenderUnderUIEvent.hpp"
#include "d2d1.h"
#include "../../../../../Client.hpp"
#include <d3d11on12.h>
#include <algorithm>
#include <codecvt>
#include <windows.h>
#include <unknwn.h>
#include <iostream>
#include <Psapi.h>
#include <tlhelp32.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui/imgui.h>
#include <imgui/imgui_impl_dx12.h>
#include <imgui/imgui_impl_win32.h>
#include "unknwnbase.h"
#include "Utils/PlatformUtils.hpp"
using ::IUnknown;

// Track if we've already called RemoveDevice() to prevent infinite loops
// Not static - needs external linkage for SwapchainHook.cpp to access
bool dx12DeviceRemoved = false;

void SwapchainHook::DX12Init() {

    Logger::debug("Initializing for DX12");

    if (FAILED(swapchain->GetDevice(IID_PPV_ARGS(d3d12Device5.put())))) {
        Logger::error("Failed to get D3D12 device from swapchain");
        return;
    }

    // Better Frames: Use RemoveDevice() to force DX11 fallback
    // After RemoveDevice(), Present() will fail and the game will recreate the swapchain with DX11
    if (!dx12DeviceRemoved && Client::settings.getSettingByName<bool>("killdx")->value)
    {
        Logger::success("[Swapchain] Better Frames: Calling RemoveDevice() to force DX11 fallback");
        d3d12Device5->RemoveDevice();
        dx12DeviceRemoved = true;
        isDX12 = false;
        d3d12Device5 = nullptr; // Release the removed device
        // Don't try to init anything - wait for game to recreate swapchain with DX11
        return;
    }

    // Warn once when rendering DX12 under Wine/Proton with Better Frames disabled:
    // Wine's translation layers don't render the D2D-on-DX12 overlay Flarial draws
    // via D3D11on12, so the mod GUI stays invisible until Better Frames is enabled.
    static bool warnedWineDX12 = false;
    if (!warnedWineDX12 && PlatformUtils::isWine())
    {
        warnedWineDX12 = true;
        Logger::warn("[Swapchain] DX12 rendering under Wine/Proton: the Flarial overlay is not "
                     "rendered by Wine in this mode. Enable \"Better Frames\" in Settings "
                     "(restarts Minecraft on DX11) to fix the invisible mod GUI.");
    }

    if (!fenceEvent) {
        fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!fenceEvent) {
            Logger::error("Failed to create fence event");
            return;
        }
    }

    IUnknown* queueAsUnknown = queue.get();
    if (!queue || !queueAsUnknown) {
        Logger::error("Failed to get D3D12 command queue");
        return;
    }
    HRESULT hr = D3D11On12CreateDevice(
        d3d12Device5.get(),
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0,
        &queueAsUnknown, 1, 0,
        d3d11Device.put(), context.put(), nullptr
    );

    if (FAILED(hr) || !d3d11Device) {
        Logger::error("Failed to create D3D11on12 device: {}", Logger::getHRESULTError(hr));
        return;
    }

    d3d11Device->QueryInterface(IID_PPV_ARGS(d3d11On12Device.put()));

    ID2D1Factory7* d2dFactory = nullptr;
    D2D1_FACTORY_OPTIONS factoryOptions = {};
    hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_MULTI_THREADED,
        __uuidof(ID2D1Factory7),
        &factoryOptions,
        (void**)&d2dFactory
    );

    if (FAILED(hr) || !d2dFactory) {
        Logger::error("Failed to create D2D1 factory: {}", Logger::getHRESULTError(hr));
        return;
    }

    IDXGIDevice* dxgiDevice = nullptr;
    d3d11On12Device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);

    ID2D1Device6* d2dDevice = nullptr;
    d2dFactory->CreateDevice(dxgiDevice, &d2dDevice);

    D2D1_DEVICE_CONTEXT_OPTIONS deviceOptions = D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS;
    d2dDevice->CreateDeviceContext(deviceOptions, D2D::context.put());

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
    swapchain->GetDesc1(&swapChainDesc);
    bufferCount = swapChainDesc.BufferCount;

    DXGISurfaces.resize(bufferCount, nullptr);
    D3D11Resources.resize(bufferCount, nullptr);
    D2D1Bitmaps.resize(bufferCount, nullptr);
    frameContexts.resize(bufferCount);

    if (!D3D12DescriptorHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = bufferCount;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        rtvHeapDesc.NodeMask = 0;

        d3d12Device5->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(D3D12DescriptorHeap.put()));
    }

    if (!d3d12DescriptorHeapImGuiRender) {
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.NumDescriptors = TOTAL_CONSOLIDATED_DESCRIPTORS;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        srvHeapDesc.NodeMask = 0;

        d3d12Device5->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(d3d12DescriptorHeapImGuiRender.put()));
    }

    if (!allocator) {
        d3d12Device5->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.put()));
    }

    if (!d3d12CommandList) {
        d3d12Device5->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr, IID_PPV_ARGS(d3d12CommandList.put()));
        d3d12CommandList->Close();
    }

    const UINT rtvDescriptorSize = d3d12Device5->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = D3D12DescriptorHeap->GetCPUDescriptorHandleForHeapStart();

    const auto dpi = static_cast<float>(GetDpiForSystem());
    D2D1_BITMAP_PROPERTIES1 bitmapProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        dpi, dpi
    );


    for (UINT i = 0; i < bufferCount; i++) {
        winrt::com_ptr<ID3D12Resource> backBuffer;
        swapchain->GetBuffer(i, IID_PPV_ARGS(backBuffer.put()));

        d3d12Device5->CreateRenderTargetView(backBuffer.get(), nullptr, rtvHandle);
        frameContexts[i].main_render_target_descriptor = rtvHandle;
        frameContexts[i].main_render_target_resource = backBuffer;
        frameContexts[i].commandAllocator = allocator;
        rtvHandle.ptr += rtvDescriptorSize;

        D3D11_RESOURCE_FLAGS d3d11Flags = { D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE };
        d3d11On12Device->CreateWrappedResource(
            backBuffer.get(), &d3d11Flags,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT,
            IID_PPV_ARGS(D3D11Resources[i].put())
        );

        D3D11Resources[i]->QueryInterface(IID_PPV_ARGS(DXGISurfaces[i].put()));

        D2D::context->CreateBitmapFromDxgiSurface(DXGISurfaces[i].get(), bitmapProps, D2D1Bitmaps[i].put());

    }

    if (!initImgui && !imguiCleanupInProgress) {
        while (imguiCleanupInProgress) {
            Sleep(1);
        }

        if (!ImGui::GetCurrentContext()) {
            ImGui::CreateContext();
        }

        auto& io = ImGui::GetIO();
        if (!io.BackendPlatformUserData) {
            ImGui_ImplWin32_Init(window2);
        }

        ImGui_ImplDX12_InitInfo initInfo = {};
        initInfo.Device = d3d12Device5.get();
        initInfo.CommandQueue = queue.get();
        initInfo.NumFramesInFlight = bufferCount;
        initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
        initInfo.SrvDescriptorHeap = d3d12DescriptorHeapImGuiRender.get();

        initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu) {
            std::lock_guard lock(descriptorAllocationMutex);

            D3D12_CPU_DESCRIPTOR_HANDLE cpu = d3d12DescriptorHeapImGuiRender->GetCPUDescriptorHandleForHeapStart();
            D3D12_GPU_DESCRIPTOR_HANDLE gpu = d3d12DescriptorHeapImGuiRender->GetGPUDescriptorHandleForHeapStart();

            *out_cpu = cpu;
            *out_gpu = gpu;
        };

        initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
        };

        if (!io.BackendRendererUserData) {
            if (!ImGui_ImplDX12_Init(&initInfo)) {
                Logger::error("Failed to initialize ImGui DX12 backend");
                return;
            }
        }
        initImgui = true;
    }

    Memory::SafeRelease(d2dDevice);
    Memory::SafeRelease(dxgiDevice);
    Memory::SafeRelease(d2dFactory);

    Blur::InitializePipeline();

    init = true;
}

void SwapchainHook::DX12Render(bool underui) {
    if (!D2D::context || !d3d11On12Device || !isDX12 || !queue) return;

    currentBitmap = swapchain->GetCurrentBackBufferIndex();

    ID3D11Resource* resource = D3D11Resources[currentBitmap].get();
    d3d11On12Device->AcquireWrappedResources(&resource, 1);


    if (currentBitmap >= bufferCount || currentBitmap >= frameContexts.size()) {
        Logger::error("DX12Render: Invalid currentBitmap index {} (bufferCount: {}, frameContexts: {})",
                     currentBitmap, bufferCount, frameContexts.size());
        return;
    }

    if (!frameContexts[currentBitmap].commandAllocator || !frameContexts[currentBitmap].main_render_target_resource) {
        Logger::error("DX12Render: Invalid frame context for index {}", currentBitmap);
        return;
    }
    D2D::context->SetTarget(D2D1Bitmaps[currentBitmap].get());
    MC::windowSize = Vec2(D2D::context->GetSize().width, D2D::context->GetSize().height);

    SaveBackbuffer();
    DX12Blur();

    D2D::context->BeginDraw();

    {
        std::lock_guard<std::mutex> lock(SwapchainHook::imguiInputMutex);
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
    }

    ImGui::Begin("t", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoDecoration);

    if (cachedDX12RTVs.size() != bufferCount) {

        for (auto rtv : cachedDX12RTVs) {
            rtv = nullptr;
        }
        cachedDX12RTVs.resize(bufferCount, nullptr);
    }

    if (!cachedDX12RTVs[currentBitmap].get()) {
        {
            winrt::com_ptr<ID3D11Texture2D> buffer2D;
            D3D11Resources[currentBitmap]->QueryInterface(IID_PPV_ARGS(buffer2D.put()));
            d3d11Device->CreateRenderTargetView(buffer2D.get(), nullptr, cachedDX12RTVs[currentBitmap].put());
        }
    }

    // Process any pending module toggles before dispatching events
    // This avoids deadlock when modules are toggled from within event callbacks
    ModuleManager::processPendingToggles();

    if (!underui) {
        auto event = nes::make_holder<RenderEvent>();
        event->RTV = cachedDX12RTVs[currentBitmap].get();
        eventMgr.trigger(event);
    } else {
        auto event = nes::make_holder<RenderUnderUIEvent>();
        event->RTV = cachedDX12RTVs[currentBitmap].get();
        eventMgr.trigger(event);
    }

    static bool notificationsShown = false;
    if (!notificationsShown && SwapchainHook::init) {
        if (auto clickGUI = ModuleManager::getModule("ClickGUI")) {
            FlarialGUI::Notify("Click " +
                clickGUI->settings.getSettingByName<std::string>("keybind")->value +
                " to open the menu in-game.");
            FlarialGUI::Notify("Join our discord! https://flarial.xyz/discord");
            notificationsShown = true;
        }
    }

    D2D::context->EndDraw();
    D2D::context->SetTarget(nullptr);

    d3d11On12Device->ReleaseWrappedResources(&resource, 1);
    context->Flush();

    frameContexts[currentBitmap].commandAllocator->Reset();

    d3d12CommandList->Reset(frameContexts[currentBitmap].commandAllocator.get(), nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = frameContexts[currentBitmap].main_render_target_resource.get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    d3d12CommandList->ResourceBarrier(1, &barrier);

    d3d12CommandList->OMSetRenderTargets(1,
        &frameContexts[currentBitmap].main_render_target_descriptor,
        FALSE, nullptr);

    ID3D12DescriptorHeap* heapPtr = d3d12DescriptorHeapImGuiRender.get();
    d3d12CommandList->SetDescriptorHeaps(1, &heapPtr);

    ImGui::End();
    ImGui::EndFrame();
    ImGui::Render();

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), d3d12CommandList.get());

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    d3d12CommandList->ResourceBarrier(1, &barrier);

    d3d12CommandList->Close();
    ID3D12CommandList* cmdListPtr = d3d12CommandList.get();

    // Verify queue is still valid before using it
    if (!queue)
    {
        Logger::error("DX12Render: Queue became null during render");
        return;
    }

    queue->ExecuteCommandLists(1, &cmdListPtr);

    if (!cachedDX12Fence)
    {
        HRESULT hr = d3d12Device5->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                               IID_PPV_ARGS(cachedDX12Fence.put()));
        if (FAILED(hr) || !cachedDX12Fence)
        {
            Logger::error("DX12Render: Failed to create fence");
            return;
        }
    }

    const UINT64 currentFenceValue = ++cachedDX12FenceValue;
    queue->Signal(cachedDX12Fence.get(), currentFenceValue);

    if (cachedDX12Fence->GetCompletedValue() < currentFenceValue) {
        cachedDX12Fence->SetEventOnCompletion(currentFenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}

void SwapchainHook::DX12Blur() {

    /* Blur Stuff */
    prepareBlur();
    if (FlarialGUI::inMenu) FlarialGUI::needsBackBuffer = true;
    else FlarialGUI::needsBackBuffer = false;
    /* Blur End */

}

bool SwapchainHook::AllocateImageDescriptor(UINT imageId, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle) {
    std::lock_guard<std::mutex> lock(descriptorAllocationMutex);

    if (!d3d12DescriptorHeapImGuiRender || !d3d12Device5) {
        Logger::custom(fg(fmt::color::red), "DescriptorAlloc", "ERROR: Consolidated descriptor heap or device not available");
        return false;
    }

    UINT descriptorIndex = (imageId - 100) + IMGUI_FONT_DESCRIPTORS;
//
    if (descriptorIndex >= TOTAL_CONSOLIDATED_DESCRIPTORS) {
        Logger::custom(fg(fmt::color::red), "DescriptorAlloc", "ERROR: Image ID {} would exceed descriptor heap capacity (index {} >= {})",
                       imageId, descriptorIndex, TOTAL_CONSOLIDATED_DESCRIPTORS);
        return false;
    }

    UINT handle_increment = d3d12Device5->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE cpu = d3d12DescriptorHeapImGuiRender->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += (handle_increment * descriptorIndex);

    D3D12_GPU_DESCRIPTOR_HANDLE gpu = d3d12DescriptorHeapImGuiRender->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += (handle_increment * descriptorIndex);

    *out_cpu_handle = cpu;
    *out_gpu_handle = gpu;

    if (descriptorIndex >= nextAvailableDescriptorIndex) {
        nextAvailableDescriptorIndex = descriptorIndex + 1;
    }


    return true;
}

UINT SwapchainHook::AllocateNextImageDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle) {
    std::lock_guard<std::mutex> lock(descriptorAllocationMutex);

    if (!d3d12DescriptorHeapImGuiRender || !d3d12Device5) {
        Logger::custom(fg(fmt::color::red), "DescriptorAlloc", "ERROR: Consolidated descriptor heap or device not available");
        return 0;
    }

    // Generate next available imageId (starting from 100 as expected by the system)
    UINT imageId = nextAvailableDescriptorIndex + 100;
    UINT descriptorIndex = nextAvailableDescriptorIndex;

    if (descriptorIndex >= TOTAL_CONSOLIDATED_DESCRIPTORS) {
        Logger::custom(fg(fmt::color::red), "DescriptorAlloc", "ERROR: No more descriptor slots available (index {} >= {})",
                       descriptorIndex, TOTAL_CONSOLIDATED_DESCRIPTORS);
        return 0;
    }

    UINT handle_increment = d3d12Device5->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE cpu = d3d12DescriptorHeapImGuiRender->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += (handle_increment * descriptorIndex);

    D3D12_GPU_DESCRIPTOR_HANDLE gpu = d3d12DescriptorHeapImGuiRender->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += (handle_increment * descriptorIndex);

    *out_cpu_handle = cpu;
    *out_gpu_handle = gpu;

    nextAvailableDescriptorIndex++;

    Logger::custom(fg(fmt::color::green), "DescriptorAlloc", "Allocated auto descriptor ID {} at index {}", imageId, descriptorIndex);

    return imageId;
}

void SwapchainHook::FreeImageDescriptor(UINT imageId) {
    std::lock_guard<std::mutex> lock(descriptorAllocationMutex);

    Logger::custom(fg(fmt::color::cyan), "DescriptorFree", "Freed descriptor for image ID {} (no-op for persistent images)", imageId);
}

void SwapchainHook::ResetDescriptorAllocation() {
    std::lock_guard<std::mutex> lock(descriptorAllocationMutex);

    nextAvailableDescriptorIndex = IMGUI_FONT_DESCRIPTORS;
    Logger::custom(fg(fmt::color::yellow), "DescriptorAlloc", "Reset descriptor allocation, next available index: {}", nextAvailableDescriptorIndex);
}

