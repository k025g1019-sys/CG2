#pragma once

#include <d3d12.h>
#include <cstdint>
#include <wrl.h>

// GPUリソース生成ヘルパ（戻り値ComPtrで所有権を渡す）

// UploadHeap上にバッファリソースを作成する
Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(ID3D12Device* device, size_t sizeInBytes);

// DepthStencil用のテクスチャリソースを作成する
Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(ID3D12Device* device, int32_t width, int32_t height);
