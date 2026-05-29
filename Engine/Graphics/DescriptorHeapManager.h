#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

class DescriptorHeapManager {
public:
    static DescriptorHeapManager* GetInstance();

    void Initialize(ID3D12Device* device, uint32_t numDescriptors);

    uint32_t Allocate(); // indexを返す

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPU(uint32_t index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPU(uint32_t index) const;

    ID3D12DescriptorHeap* GetHeap() const { return heap_.Get(); }

    uint32_t GetDescriptorSize() const { return descriptorSize_; }

private:
    DescriptorHeapManager() = default;

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;

    uint32_t descriptorSize_ = 0;
    uint32_t currentIndex_ = 0;
    uint32_t maxCount_ = 0;
};