#pragma once
#include <d3d12.h>
#include <vector>
#include <string>
#include "externals/DirectXTex/DirectXTex.h"

class TextureManager {
public:
    static TextureManager* GetInstance();

    void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);

    uint32_t LoadTexture(const std::string& path);

    ID3D12Resource* GetResource(uint32_t index);
    DirectX::TexMetadata GetMetadata(uint32_t index);
    D3D12_GPU_DESCRIPTOR_HANDLE GetSRV(uint32_t index);

private:
    struct Texture {
        DirectX::ScratchImage image;
        DirectX::TexMetadata metadata{};
        ID3D12Resource* resource = nullptr;
        ID3D12Resource* upload = nullptr;

        Texture() = default;

        // コピー禁止
        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;

        // move許可
        Texture(Texture&&) = default;
        Texture& operator=(Texture&&) = default;
    };

    std::vector<Texture> textures_;

    ID3D12Device* device_ = nullptr;
    ID3D12GraphicsCommandList* cmdList_ = nullptr;
    std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> srvHandlesGPU_;
};