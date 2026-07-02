#pragma once

#include <d3d12.h>
#include <cstdint>

/// <summary>
/// ディスクリプタヒープの生成ヘルパと、SRVヒープのスロット割り当てを行う。
/// SRVヒープのスロット（index）はAllocateSrvで払い出し、使用者（ImGui・各テクスチャ）が
/// 自分のスロットを確保する。indexのハードコードによる衝突を防ぐ。
/// </summary>
class DescriptorHeapManager {
public:

    static DescriptorHeapManager* GetInstance();

    // 割り当て対象のSRVヒープを登録する（DirectXCore初期化後に1回呼ぶ）
    void Initialize(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap);

    // SRVヒープの空きスロットを1つ払い出し、そのindexを返す
    uint32_t AllocateSrv();

    // 指定スロットのCPU/GPUハンドルを取得する
    D3D12_CPU_DESCRIPTOR_HANDLE GetSrvCPUHandle(uint32_t index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGPUHandle(uint32_t index) const;

public:

    // DescriptorHeap作成
    static ID3D12DescriptorHeap* CreateDescriptorHeap(
        ID3D12Device* device,
        D3D12_DESCRIPTOR_HEAP_TYPE heapType,
        UINT numDescriptors,
        bool shaderVisible
    );

    // CPUハンドル取得
    static D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(
        ID3D12DescriptorHeap* descriptorHeap,
        uint32_t descriptorSize,
        uint32_t index
    );

    // GPUハンドル取得
    static D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(
        ID3D12DescriptorHeap* descriptorHeap,
        uint32_t descriptorSize,
        uint32_t index
    );

private:

    DescriptorHeapManager() = default;

    ~DescriptorHeapManager() = default;

    DescriptorHeapManager(const DescriptorHeapManager&) = delete;

    DescriptorHeapManager& operator=(const DescriptorHeapManager&) = delete;

private:

    ID3D12DescriptorHeap* srvHeap_ = nullptr;  // 非所有（DirectXCoreが所有）

    uint32_t descriptorSize_ = 0;  // SRVディスクリプタ1つ分のサイズ

    uint32_t capacity_ = 0;   // SRVヒープのスロット総数

    uint32_t nextIndex_ = 0;  // 次に払い出すスロット
};
