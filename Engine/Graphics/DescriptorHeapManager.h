#pragma once

#include <d3d12.h>
#include <cstdint>

class DescriptorHeapManager {
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
};