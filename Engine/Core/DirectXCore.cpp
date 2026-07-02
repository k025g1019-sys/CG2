#include "Engine/Core/DirectXCore.h"

#include <cassert>

#include <d3d12sdklayers.h>

#include "Engine/Graphics/GpuResource.h"

#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")

using Microsoft::WRL::ComPtr;

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

    InitializeDepthStencil(width, height);

    InitializeFence();

    UpdateViewportAndScissor(width, height);
}

void DirectXCore::InitializeDXGIDevice() {

#ifdef _DEBUG

    ComPtr<ID3D12Debug1> debugController;

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

        useAdapter_.Reset();
    }

    assert(useAdapter_ != nullptr);

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0
    };

    for (size_t i = 0; i < _countof(featureLevels); ++i) {

        hr = D3D12CreateDevice(
            useAdapter_.Get(),
            featureLevels[i],
            IID_PPV_ARGS(&device_)
        );

        if (SUCCEEDED(hr)) {

            break;
        }
    }

    assert(device_ != nullptr);

#ifdef _DEBUG
    // 危険なエラーで停止し、既知の偽エラーは抑制する
    {
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
            // Windows11のDXGI/DX12デバッグレイヤー相互作用バグによるエラーを抑制
            // https://stackoverflow.com/questions/69805245/directx-12-application-is-crashing-in-windows-11
            D3D12_MESSAGE_ID denyIds[] = {
                D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
            };
            D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
            D3D12_INFO_QUEUE_FILTER filter{};
            filter.DenyList.NumIDs = _countof(denyIds);
            filter.DenyList.pIDList = denyIds;
            filter.DenyList.NumSeverities = _countof(severities);
            filter.DenyList.pSeverityList = severities;
            infoQueue->PushStorageFilter(&filter);
        }
    }
#endif
}

void DirectXCore::InitializeCommand() {

    HRESULT hr;

    D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};

    hr = device_->CreateCommandQueue(
        &commandQueueDesc,
        IID_PPV_ARGS(&commandQueue_)
    );

    assert(SUCCEEDED(hr));

    // フレームごとのコマンドアロケータを作る（CPU/GPUの並列化のため）
    for (auto& commandAllocator : commandAllocators_) {

        hr = device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&commandAllocator)
        );

        assert(SUCCEEDED(hr));
    }

    hr = device_->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        commandAllocators_[0].Get(),
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

    ComPtr<IDXGISwapChain1> swapChain1;

    HRESULT hr = dxgiFactory_->CreateSwapChainForHwnd(
        commandQueue_.Get(),
        hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain1
    );

    assert(SUCCEEDED(hr));

    // IDXGISwapChain4へ変換して保持する
    hr = swapChain1.As(&swapChain_);

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

    CreateSwapChainRenderTargets();
}

void DirectXCore::CreateSwapChainRenderTargets() {

    for (uint32_t i = 0; i < kSwapChainBufferCount; ++i) {

        [[maybe_unused]] HRESULT hr = swapChain_->GetBuffer(
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
            swapChainResources_[i].Get(),
            &rtvDesc,
            rtvHandles_[i]
        );
    }
}

void DirectXCore::InitializeDepthStencil(int32_t width, int32_t height) {

    depthStencilResource_ =
        CreateDepthStencilTextureResource(device_.Get(), width, height);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};

    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    device_->CreateDepthStencilView(
        depthStencilResource_.Get(),
        &dsvDesc,
        dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart()
    );
}

void DirectXCore::UpdateViewportAndScissor(int32_t width, int32_t height) {

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

void DirectXCore::InitializeFence() {

    [[maybe_unused]] HRESULT hr = device_->CreateFence(
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

void DirectXCore::Resize(int32_t width, int32_t height) {

    // 初期化前、または最小化などで0サイズになった場合は何もしない
    if (!swapChain_ || width <= 0 || height <= 0) {
        return;
    }

    // 既に同じサイズなら作り直し不要
    if (width == scissorRect_.right && height == scissorRect_.bottom) {
        return;
    }

    // 古いバッファを参照したままのGPU処理が無いように完了を待つ
    WaitForGPU();

    // ResizeBuffersの前に、既存のバックバッファと深度バッファの参照を解放する
    for (auto& resource : swapChainResources_) {
        resource.Reset();
    }
    depthStencilResource_.Reset();

    // スワップチェーンのバッファをウィンドウサイズに合わせて作り直す
    [[maybe_unused]] HRESULT hr = swapChain_->ResizeBuffers(
        kSwapChainBufferCount,
        static_cast<UINT>(width),
        static_cast<UINT>(height),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        0
    );

    assert(SUCCEEDED(hr));

    // RTV・深度バッファ・ビューポートを新しいサイズで作り直す
    CreateSwapChainRenderTargets();

    InitializeDepthStencil(width, height);

    UpdateViewportAndScissor(width, height);
}

void DirectXCore::BeginFrame() {

    UINT backBufferIndex =
        swapChain_->GetCurrentBackBufferIndex();

    D3D12_RESOURCE_BARRIER barrier{};

    barrier.Type =
        D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

    barrier.Transition.pResource =
        swapChainResources_[backBufferIndex].Get();

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

    // 描画で使うSRVディスクリプタヒープを設定する（シーン・ImGui共通）
    ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap_.Get() };
    commandList_->SetDescriptorHeaps(1, descriptorHeaps);
}

void DirectXCore::WaitForGPU() {

    ++fenceValue_;

    commandQueue_->Signal(fence_.Get(), fenceValue_);

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
        swapChainResources_[backBufferIndex].Get();

    barrier.Transition.StateBefore =
        D3D12_RESOURCE_STATE_RENDER_TARGET;

    barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_PRESENT;

    commandList_->ResourceBarrier(1, &barrier);

    HRESULT hr = commandList_->Close();

    assert(SUCCEEDED(hr));

    ID3D12CommandList* commandLists[] = {
        commandList_.Get()
    };

    commandQueue_->ExecuteCommandLists(
        1,
        commandLists
    );

    swapChain_->Present(1, 0);

    // このフレームの完了を示すフェンス値を記録してシグナルする
    // （完了は待たない。CPUはすぐ次フレームの準備に進める）
    ++fenceValue_;

    frameFenceValues_[backBufferIndex] = fenceValue_;

    commandQueue_->Signal(
        fence_.Get(),
        fenceValue_
    );

    // 次に使うフレームスロット（Presentでバックバッファが切り替わった後のindex）の
    // 前回の描画がまだ終わっていなければ、ここで完了を待つ。
    // これによりCPUはGPUよりkFramesInFlight-1フレームだけ先行できる。
    UINT nextFrameIndex = swapChain_->GetCurrentBackBufferIndex();

    if (fence_->GetCompletedValue() < frameFenceValues_[nextFrameIndex]) {

        fence_->SetEventOnCompletion(
            frameFenceValues_[nextFrameIndex],
            fenceEvent_
        );

        WaitForSingleObject(
            fenceEvent_,
            INFINITE
        );
    }

    // 完了済みになった次フレーム用アロケータでコマンドリストをリセットする
    hr = commandAllocators_[nextFrameIndex]->Reset();

    assert(SUCCEEDED(hr));

    hr = commandList_->Reset(
        commandAllocators_[nextFrameIndex].Get(),
        nullptr
    );

    assert(SUCCEEDED(hr));
}

void DirectXCore::Finalize() {

    // 実行中のフレームが残っているとリソース解放でGPUがクラッシュするため、完了を待つ
    WaitForGPU();

    CloseHandle(fenceEvent_);

    // 各リソースはComPtrにより自動開放されるが、
    // リソースリークチェック前に確実に解放するため明示的にResetする。
    fence_.Reset();

    for (auto& resource : swapChainResources_) {
        resource.Reset();
    }

    depthStencilResource_.Reset();

    srvDescriptorHeap_.Reset();

    dsvDescriptorHeap_.Reset();

    rtvDescriptorHeap_.Reset();

    swapChain_.Reset();

    commandList_.Reset();

    for (auto& commandAllocator : commandAllocators_) {
        commandAllocator.Reset();
    }

    commandQueue_.Reset();

    device_.Reset();

    useAdapter_.Reset();

    dxgiFactory_.Reset();
}

D3D12_CPU_DESCRIPTOR_HANDLE
DirectXCore::GetCurrentRTVHandle() const {

    UINT backBufferIndex =
        swapChain_->GetCurrentBackBufferIndex();

    return rtvHandles_[backBufferIndex];
}

uint32_t DirectXCore::GetFrameIndex() const {

    // バックバッファ数とフレームインフライト数は一致している前提
    static_assert(kFramesInFlight == kSwapChainBufferCount);

    return swapChain_->GetCurrentBackBufferIndex();
}
