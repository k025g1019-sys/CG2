#include "TextureManager.h"
#include "ConvertString.h"
#include <cassert>

TextureManager* TextureManager::GetInstance() {
    static TextureManager instance;
    return &instance;
}

void TextureManager::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList) {
    device_ = device;
    cmdList_ = cmdList;
}

uint32_t TextureManager::LoadTexture(const std::string& path) {
    Texture tex;

    std::wstring w = ConvertString(path);

    HRESULT hr = DirectX::LoadFromWICFile(
        w.c_str(),
        DirectX::WIC_FLAGS_FORCE_SRGB,
        nullptr,
        tex.image
    );
    assert(SUCCEEDED(hr));

    DirectX::GenerateMipMaps(
        tex.image.GetImages(),
        tex.image.GetImageCount(),
        tex.image.GetMetadata(),
        DirectX::TEX_FILTER_SRGB,
        0,
        tex.image
    );

    tex.metadata = tex.image.GetMetadata();

    textures_.emplace_back(std::move(tex));
    return (uint32_t)textures_.size() - 1;
}

ID3D12Resource* TextureManager::GetResource(uint32_t index) {
    return textures_[index].resource;
}

DirectX::TexMetadata TextureManager::GetMetadata(uint32_t index) {
    return textures_[index].metadata;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSRV(uint32_t index) {
    return srvHandlesGPU_[index];
}