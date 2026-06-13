#pragma once

#include <cstdint>

#include <Windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

class DirectXCore {
public:

    static DirectXCore* GetInstance();

    void Initialize(HWND hwnd, int32_t width, int32_t height);

    void BeginFrame();

    void WaitForGPU();

    void EndFrame();

    void Finalize();

public:

    ID3D12Device* GetDevice() const {
        return device_.Get();
    }

    ID3D12GraphicsCommandList* GetCommandList() const {
        return commandList_.Get();
    }

    ID3D12CommandQueue* GetCommandQueue() const {
        return commandQueue_.Get();
    }

    IDXGISwapChain4* GetSwapChain() const {
        return swapChain_.Get();
    }

    static uint32_t GetSwapChainBufferCount() {
        return kSwapChainBufferCount;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTVHandle() const;

public:

    ID3D12DescriptorHeap* GetSRVDescriptorHeap() const {
        return srvDescriptorHeap_.Get();
    }

    ID3D12DescriptorHeap* GetDSVDescriptorHeap() const {
        return dsvDescriptorHeap_.Get();
    }

    D3D12_VIEWPORT GetViewport() const { return viewport_; }

    D3D12_RECT GetScissorRect() const { return scissorRect_; }

private:

    DirectXCore() = default;

    ~DirectXCore() = default;

    DirectXCore(const DirectXCore&) = delete;

    DirectXCore& operator=(const DirectXCore&) = delete;

private:

    void InitializeDXGIDevice();

    void InitializeCommand();

    void InitializeSwapChain(HWND hwnd, int32_t width, int32_t height);

    void InitializeDescriptorHeaps();

    void InitializeRenderTarget();

    void InitializeFence();

private:

    Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;

    Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter_;

    Microsoft::WRL::ComPtr<ID3D12Device> device_;

private:

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;

private:

    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;

private:

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;

    static const uint32_t kSwapChainBufferCount = 2;

    Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources_[kSwapChainBufferCount];

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[kSwapChainBufferCount];

    uint32_t descriptorSizeRTV_ = 0;

private:

    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;

    HANDLE fenceEvent_ = nullptr;

    uint64_t fenceValue_ = 0;

private:

    D3D12_VIEWPORT viewport_{};

    D3D12_RECT scissorRect_{};
};
