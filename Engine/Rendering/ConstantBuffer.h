#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>
#include <d3d12.h>
#include <wrl.h>

#include "Engine/Graphics/GpuResource.h"

/// <summary>
/// 型付きの定数バッファ（CBV用アップロードバッファ）。
/// フレームインフライト数ぶんのスロットを持ち、CPUが書くスロットと
/// GPUが読むスロットを分けることで、描画中フレームのデータを壊さない。
/// 毎フレーム Write(frameIndex, data) で書き込み、GetGPUAddress(frameIndex) をバインドする。
/// </summary>
template <typename T>
class ConstantBuffer {
public:

    // slotCountには通常DirectXCore::kFramesInFlightを渡す
    void Create(ID3D12Device* device, uint32_t slotCount) {
        // CBVのアドレスは256バイトアラインメントが必要
        slotSize_ = (uint32_t(sizeof(T)) + 255u) & ~255u;
        slotCount_ = slotCount;
        resource_ = CreateBufferResource(device, size_t(slotSize_) * slotCount);
        resource_->Map(0, nullptr, reinterpret_cast<void**>(&mapped_));
    }

    // 指定スロットへデータを書き込む
    void Write(uint32_t slot, const T& data) {
        assert(mapped_ != nullptr);
        assert(slot < slotCount_);
        std::memcpy(mapped_ + size_t(slot) * slotSize_, &data, sizeof(T));
    }

    // 指定スロットのGPU仮想アドレス（SetGraphicsRootConstantBufferViewに渡す）
    D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress(uint32_t slot) const {
        assert(resource_ != nullptr);
        assert(slot < slotCount_);
        return resource_->GetGPUVirtualAddress() + UINT64(slot) * slotSize_;
    }

    // リソースを明示的に解放する。
    // （静的シングルトンのメンバとして持つ場合、リークチェックより前に解放するために呼ぶ）
    void Reset() {
        resource_.Reset();
        mapped_ = nullptr;
        slotCount_ = 0;
    }

private:

    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;

    uint8_t* mapped_ = nullptr;  // Map済みの書き込み先（resource_生存中のみ有効）

    uint32_t slotSize_ = 0;   // 256バイトアラインメント済みの1スロットのサイズ

    uint32_t slotCount_ = 0;
};
