#include "DescriptorHeapManager.h"
#include <cassert>

using Microsoft::WRL::ComPtr;

DescriptorHeapManager* DescriptorHeapManager::GetInstance() {
    static DescriptorHeapManager instance;
    return &instance;
}

void DescriptorHeapManager::Initialize(ID3D12Device* device, uint32_t numDescriptors) {
    maxCount_ = numDescriptors;

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.NumDescriptors = numDescriptors;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_));
    assert(SUCCEEDED(hr));

    descriptorSize_ =
        device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

uint32_t DescriptorHeapManager::Allocate() {
    assert(currentIndex_ < maxCount_);
    return currentIndex_++;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeapManager::GetCPU(uint32_t index) const {
    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        heap_->GetCPUDescriptorHandleForHeapStart();

    handle.ptr += index * descriptorSize_;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeapManager::GetGPU(uint32_t index) const {
    D3D12_GPU_DESCRIPTOR_HANDLE handle =
        heap_->GetGPUDescriptorHandleForHeapStart();

    handle.ptr += index * descriptorSize_;
    return handle;
}