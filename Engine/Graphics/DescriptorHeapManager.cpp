#include "Engine/Graphics/DescriptorHeapManager.h"
#include <cassert>

DescriptorHeapManager* DescriptorHeapManager::GetInstance() {
    static DescriptorHeapManager instance;
    return &instance;
}

void DescriptorHeapManager::Initialize(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap) {
    assert(device != nullptr);
    assert(srvHeap != nullptr);

    srvHeap_ = srvHeap;
    descriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    capacity_ = srvHeap->GetDesc().NumDescriptors;
    nextIndex_ = 0;
}

uint32_t DescriptorHeapManager::AllocateSrv() {
    // ヒープが満杯なら割り当て失敗（capacityを増やすこと）
    assert(nextIndex_ < capacity_);
    return nextIndex_++;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeapManager::GetSrvCPUHandle(uint32_t index) const {
    assert(srvHeap_ != nullptr);
    return GetCPUDescriptorHandle(srvHeap_, descriptorSize_, index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeapManager::GetSrvGPUHandle(uint32_t index) const {
    assert(srvHeap_ != nullptr);
    return GetGPUDescriptorHandle(srvHeap_, descriptorSize_, index);
}

ID3D12DescriptorHeap* DescriptorHeapManager::CreateDescriptorHeap(
    ID3D12Device* device,
    D3D12_DESCRIPTOR_HEAP_TYPE heapType,
    UINT numDescriptors,
    bool shaderVisible
) {
    ID3D12DescriptorHeap* descriptorHeap = nullptr;

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = heapType;
    desc.NumDescriptors = numDescriptors;
    desc.Flags =
        shaderVisible
        ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
        : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    [[maybe_unused]] HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap));
    assert(SUCCEEDED(hr));

    return descriptorHeap;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeapManager::GetCPUDescriptorHandle(
    ID3D12DescriptorHeap* descriptorHeap,
    uint32_t descriptorSize,
    uint32_t index
) {
    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        descriptorHeap->GetCPUDescriptorHandleForHeapStart();

    handle.ptr += static_cast<SIZE_T>(descriptorSize) * index;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeapManager::GetGPUDescriptorHandle(
    ID3D12DescriptorHeap* descriptorHeap,
    uint32_t descriptorSize,
    uint32_t index
) {
    D3D12_GPU_DESCRIPTOR_HANDLE handle =
        descriptorHeap->GetGPUDescriptorHandleForHeapStart();

    handle.ptr += static_cast<UINT64>(descriptorSize) * index;
    return handle;
}
