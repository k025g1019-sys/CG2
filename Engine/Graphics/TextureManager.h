#pragma once

#include <d3d12.h>
#include <string>
#include <vector>
#include <wrl.h>
#include "externals/DirectXTex/DirectXTex.h"

struct TextureData {
    DirectX::ScratchImage mipImage;
    DirectX::TexMetadata metadata;
    Microsoft::WRL::ComPtr<ID3D12Resource> textureResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource;
};

class TextureManager {
public:
    // テクスチャを読み込み、GPUへ転送して使える状態のTextureDataを返す
    static TextureData LoadAndUpload(
        const std::string& filepath,
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList);

    static TextureData LoadTexture(const std::string& filepath);

    static Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(
        ID3D12Device* device,
        const DirectX::TexMetadata& metadata);

    static Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(
        ID3D12Resource* texture,
        const DirectX::ScratchImage& mipImages,
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList);
};
