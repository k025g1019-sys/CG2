#include "DirectXCore.h"

#include <cassert>

#include <d3d12sdklayers.h>

#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")

DirectXCore* DirectXCore::GetInstance() {

    static DirectXCore instance;

    return &instance;
}

void DirectXCore::Initialize(
    HWND hwnd,
    int32_t width,
    int32_t height
) {

    InitializeDXGIDevice();

    InitializeCommand();

    InitializeSwapChain(hwnd, width, height);

    InitializeDescriptorHeaps();

    InitializeRenderTarget();

    InitializeFence();

    viewport_.Width = static_cast<float>(width);
    viewport_.Height = static_cast<float>(height);
    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;

    scissorRect_.left = 0;
    scissorRect_.right = width;
    scissorRect_.top = 0;
    scissorRect_.bottom = height;
}

void DirectXCore::InitializeDXGIDevice() {

#ifdef _DEBUG

    ID3D12Debug1* debugController = nullptr;

    if (SUCCEEDED(D3D12GetDebugInterface(
        IID_PPV_ARGS(&debugController)))) {

        debugController->EnableDebugLayer();

        debugController->SetEnableGPUBasedValidation(TRUE);
    }

#endif

    HRESULT hr = CreateDXGIFactory(
        IID_PPV_ARGS(&dxgiFactory_)
    );

    assert(SUCCEEDED(hr));

    for (
        UINT i = 0;
        dxgiFactory_->EnumAdapterByGpuPreference(
        i,
        DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
        IID_PPV_ARGS(&useAdapter_)
        ) != DXGI_ERROR_NOT_FOUND;
        ++i
        ) {

        DXGI_ADAPTER_DESC3 adapterDesc{};

        hr = useAdapter_->GetDesc3(&adapterDesc);

        assert(SUCCEEDED(hr));

        if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {

            break;
        }

        useAdapter_->Release();

        useAdapter_ = nullptr;
    }

    assert(useAdapter_ != nullptr);

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0
    };

    for (size_t i = 0; i < _countof(featureLevels); ++i) {

        hr = D3D12CreateDevice(
            useAdapter_,
            featureLevels[i],
            IID_PPV_ARGS(&device_)
        );

        if (SUCCEEDED(hr)) {

            break;
        }
    }

    assert(device_ != nullptr);
}

void DirectXCore::InitializeCommand() {

    HRESULT hr;

    D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};

    hr = device_->CreateCommandQueue(
        &commandQueueDesc,
        IID_PPV_ARGS(&commandQueue_)
    );

    assert(SUCCEEDED(hr));

    hr = device_->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&commandAllocator_)
    );

    assert(SUCCEEDED(hr));

    hr = device_->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        commandAllocator_,
        nullptr,
        IID_PPV_ARGS(&commandList_)
    );

    assert(SUCCEEDED(hr));
}

void DirectXCore::InitializeSwapChain(
    HWND hwnd,
    int32_t width,
    int32_t height
) {

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};

    swapChainDesc.Width = width;

    swapChainDesc.Height = height;

    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    swapChainDesc.SampleDesc.Count = 1;

    swapChainDesc.BufferUsage =
        DXGI_USAGE_RENDER_TARGET_OUTPUT;

    swapChainDesc.BufferCount =
        kSwapChainBufferCount;

    swapChainDesc.SwapEffect =
        DXGI_SWAP_EFFECT_FLIP_DISCARD;

    HRESULT hr = dxgiFactory_->CreateSwapChainForHwnd(
        commandQueue_,
        hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        reinterpret_cast<IDXGISwapChain1**>(&swapChain_)
    );

    assert(SUCCEEDED(hr));
}

void DirectXCore::InitializeDescriptorHeaps() {

    HRESULT hr;

    // SRV Heap
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.NumDescriptors = 128;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    hr = device_->CreateDescriptorHeap(
        &srvHeapDesc,
        IID_PPV_ARGS(&srvDescriptorHeap_)
    );

    assert(SUCCEEDED(hr));

    // DSV Heap
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    hr = device_->CreateDescriptorHeap(
        &dsvHeapDesc,
        IID_PPV_ARGS(&dsvDescriptorHeap_)
    );

    assert(SUCCEEDED(hr));
}

void DirectXCore::InitializeRenderTarget() {

    HRESULT hr;

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};

    rtvHeapDesc.Type =
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

    rtvHeapDesc.NumDescriptors =
        kSwapChainBufferCount;

    hr = device_->CreateDescriptorHeap(
        &rtvHeapDesc,
        IID_PPV_ARGS(&rtvDescriptorHeap_)
    );

    assert(SUCCEEDED(hr));

    descriptorSizeRTV_ =
        device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV
        );

    for (uint32_t i = 0; i < kSwapChainBufferCount; ++i) {

        hr = swapChain_->GetBuffer(
            i,
            IID_PPV_ARGS(&swapChainResources_[i])
        );

        assert(SUCCEEDED(hr));

        rtvHandles_[i] =
            rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

        rtvHandles_[i].ptr +=
            descriptorSizeRTV_ * i;

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};

        rtvDesc.Format =
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

        rtvDesc.ViewDimension =
            D3D12_RTV_DIMENSION_TEXTURE2D;

        device_->CreateRenderTargetView(
            swapChainResources_[i],
            &rtvDesc,
            rtvHandles_[i]
        );
    }
}

void DirectXCore::InitializeFence() {

    HRESULT hr = device_->CreateFence(
        fenceValue_,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&fence_)
    );

    assert(SUCCEEDED(hr));

    fenceEvent_ = CreateEvent(
        nullptr,
        FALSE,
        FALSE,
        nullptr
    );

    assert(fenceEvent_ != nullptr);
}

void DirectXCore::BeginFrame() {

    UINT backBufferIndex =
        swapChain_->GetCurrentBackBufferIndex();

    D3D12_RESOURCE_BARRIER barrier{};

    barrier.Type =
        D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

    barrier.Transition.pResource =
        swapChainResources_[backBufferIndex];

    barrier.Transition.StateBefore =
        D3D12_RESOURCE_STATE_PRESENT;

    barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_RENDER_TARGET;

    commandList_->ResourceBarrier(1, &barrier);

    commandList_->RSSetViewports(1, &viewport_);

    commandList_->RSSetScissorRects(1, &scissorRect_);

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
        dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

    commandList_->OMSetRenderTargets(
        1,
        &rtvHandles_[backBufferIndex],
        false,
        &dsvHandle
    );

    float clearColor[] = {
        0.1f,
        0.25f,
        0.5f,
        1.0f
    };

    commandList_->ClearDepthStencilView(
        dsvHandle,
        D3D12_CLEAR_FLAG_DEPTH,
        1.0f,
        0,
        0,
        nullptr
    );

    commandList_->ClearRenderTargetView(
        rtvHandles_[backBufferIndex],
        clearColor,
        0,
        nullptr
    );
}

void DirectXCore::WaitForGPU() {

    ++fenceValue_;

    commandQueue_->Signal(fence_, fenceValue_);

    if (fence_->GetCompletedValue() < fenceValue_) {

        fence_->SetEventOnCompletion(
            fenceValue_,
            fenceEvent_
        );

        WaitForSingleObject(
            fenceEvent_,
            INFINITE
        );
    }
}

void DirectXCore::EndFrame() {

    UINT backBufferIndex =
        swapChain_->GetCurrentBackBufferIndex();

    D3D12_RESOURCE_BARRIER barrier{};

    barrier.Type =
        D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

    barrier.Transition.pResource =
        swapChainResources_[backBufferIndex];

    barrier.Transition.StateBefore =
        D3D12_RESOURCE_STATE_RENDER_TARGET;

    barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_PRESENT;

    commandList_->ResourceBarrier(1, &barrier);

    HRESULT hr = commandList_->Close();

    assert(SUCCEEDED(hr));

    ID3D12CommandList* commandLists[] = {
        commandList_
    };

    commandQueue_->ExecuteCommandLists(
        1,
        commandLists
    );

    swapChain_->Present(1, 0);

    ++fenceValue_;

    commandQueue_->Signal(
        fence_,
        fenceValue_
    );

    if (fence_->GetCompletedValue() < fenceValue_) {

        fence_->SetEventOnCompletion(
            fenceValue_,
            fenceEvent_
        );

        WaitForSingleObject(
            fenceEvent_,
            INFINITE
        );
    }

    hr = commandAllocator_->Reset();

    assert(SUCCEEDED(hr));

    hr = commandList_->Reset(
        commandAllocator_,
        nullptr
    );

    assert(SUCCEEDED(hr));
}

void DirectXCore::Finalize() {

    CloseHandle(fenceEvent_);

    fence_->Release();

    for (uint32_t i = 0; i < kSwapChainBufferCount; ++i) {

        swapChainResources_[i]->Release();
    }

    srvDescriptorHeap_->Release();

    dsvDescriptorHeap_->Release();

    rtvDescriptorHeap_->Release();

    swapChain_->Release();

    commandList_->Release();

    commandAllocator_->Release();

    commandQueue_->Release();

    device_->Release();

    useAdapter_->Release();

    dxgiFactory_->Release();
}

D3D12_CPU_DESCRIPTOR_HANDLE
DirectXCore::GetCurrentRTVHandle() const {

    UINT backBufferIndex =
        swapChain_->GetCurrentBackBufferIndex();

    return rtvHandles_[backBufferIndex];
}