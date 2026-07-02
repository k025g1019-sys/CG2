#pragma once

#include <cstdint>

#include <Windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

class DirectXCore {
public:

    // CPUが先行して準備できるフレーム数。
    // 定数バッファ等の毎フレーム書き換えるリソースは、この数だけスロットを持ち、
    // GPUが読んでいる最中のスロットへCPUが書き込まないようにする。
    static constexpr uint32_t kFramesInFlight = 2;

    static DirectXCore* GetInstance();

    // 現在のフレームスロット（0〜kFramesInFlight-1）。定数バッファのスロット選択に使う
    uint32_t GetFrameIndex() const;

    void Initialize(HWND hwnd, int32_t width, int32_t height);

    // ウィンドウサイズ変更に追従して、スワップチェーンや深度バッファ等を作り直す
    void Resize(int32_t width, int32_t height);

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

    // スワップチェーンの各バッファに対するRTVを生成する（リサイズ時の作り直しにも使う）
    void CreateSwapChainRenderTargets();

    // 深度バッファ（DepthStencil）リソースとDSVを生成する
    void InitializeDepthStencil(int32_t width, int32_t height);

    void InitializeFence();

    // ビューポートとシザー矩形を指定サイズへ更新する
    void UpdateViewportAndScissor(int32_t width, int32_t height);

private:

    Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;

    Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter_;

    Microsoft::WRL::ComPtr<ID3D12Device> device_;

private:

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;

    // フレームごとのコマンドアロケータ。
    // GPUが前フレームを実行中でも、CPUは別のアロケータで次フレームを記録できる。
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocators_[kFramesInFlight];

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

    // 深度バッファ本体（DSVはdsvDescriptorHeap_の先頭に作成する）
    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;

private:

    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;

    HANDLE fenceEvent_ = nullptr;

    uint64_t fenceValue_ = 0;

    // 各フレームスロットの描画完了を示すフェンス値。
    // EndFrameで記録し、同じスロットを再利用する前にこの値の完了を待つ。
    uint64_t frameFenceValues_[kFramesInFlight] = {};

    // 現在のフレームスロット（0〜kFramesInFlight-1）。EndFrameで次へ進む。
    // バックバッファindexはResizeBuffersで0にリセットされ「記録中のアロケータ」と
    // ずれることがあるため、スワップチェーンとは独立に自前で管理する。
    uint32_t frameIndex_ = 0;

private:

    D3D12_VIEWPORT viewport_{};

    D3D12_RECT scissorRect_{};
};
