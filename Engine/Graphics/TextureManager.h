#pragma once

#include <d3d12.h>
#include <string>
#include <vector>
#include "externals/DirectXTex/DirectXTex.h"

struct TextureData {
    DirectX::ScratchImage mipImage;
    DirectX::TexMetadata metadata;
    ID3D12Resource* textureResource = nullptr;
    ID3D12Resource* intermediateResource = nullptr;
};

class TextureManager {
public:
    static TextureData LoadTexture(const std::string& filepath);

    static ID3D12Resource* CreateTextureResource(
        ID3D12Device* device,
        const DirectX::TexMetadata& metadata);

    static ID3D12Resource* UploadTextureData(
        ID3D12Resource* texture,
        const DirectX::ScratchImage& mipImages,
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList);

private:
    static ID3D12Resource* CreateBufferResource(
        ID3D12Device* device,
        size_t sizeInBytes);
};