#include "Engine/Graphics/TextureManager.h"
#include <cassert>
#include <wrl.h>
#include "Engine/String/ConvertString.h"
#include "Engine/Graphics/GpuResource.h"
#include "Engine/Graphics/DescriptorHeapManager.h"
#include "externals/DirectXTex/d3dx12.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

TextureManager* TextureManager::GetInstance() {
    static TextureManager instance;
    return &instance;
}

void TextureManager::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* commandList) {
    assert(device != nullptr);
    assert(commandList != nullptr);

    device_ = device;
    commandList_ = commandList;
}

uint32_t TextureManager::Load(const std::string& filepath) {
    assert(device_ != nullptr && "TextureManager::Initializeが呼ばれていない");

    // 読み込み済みならキャッシュから返す
    for (uint32_t i = 0; i < textures_.size(); ++i) {
        if (textures_[i].filepath == filepath) {
            return i;
        }
    }

    // 読み込み → リソース作成 → 転送コマンド発行
    Texture texture;
    texture.filepath = filepath;

    ScratchImage mipImages = LoadTextureImage(filepath);
    texture.metadata = mipImages.GetMetadata();
    texture.resource = CreateTextureResource(device_, texture.metadata);
    texture.intermediate = UploadTextureData(texture.resource.Get(), mipImages, device_, commandList_);

    // SRVスロットを確保して作成する
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = texture.metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(texture.metadata.mipLevels);

    DescriptorHeapManager* heapManager = DescriptorHeapManager::GetInstance();
    uint32_t srvIndex = heapManager->AllocateSrv();
    device_->CreateShaderResourceView(
        texture.resource.Get(), &srvDesc, heapManager->GetSrvCPUHandle(srvIndex));
    texture.srvHandleGPU = heapManager->GetSrvGPUHandle(srvIndex);

    textures_.push_back(std::move(texture));
    return uint32_t(textures_.size() - 1);
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(uint32_t textureHandle) const {
    assert(textureHandle < textures_.size());
    return textures_[textureHandle].srvHandleGPU;
}

void TextureManager::Finalize() {
    textures_.clear();
    device_ = nullptr;
    commandList_ = nullptr;
}

ScratchImage TextureManager::LoadTextureImage(const std::string& filepath) {
    std::wstring filePathW = ConvertString(filepath);

    ScratchImage image;
    HRESULT hr = LoadFromWICFile(
        filePathW.c_str(),
        WIC_FLAGS_FORCE_SRGB,
        nullptr,
        image);
    assert(SUCCEEDED(hr));

    ScratchImage mipImages;
    hr = GenerateMipMaps(
        image.GetImages(),
        image.GetImageCount(),
        image.GetMetadata(),
        TEX_FILTER_SRGB,
        0,
        mipImages);
    assert(SUCCEEDED(hr));

    return mipImages;
}

ComPtr<ID3D12Resource> TextureManager::CreateTextureResource(
    ID3D12Device* device,
    const DirectX::TexMetadata& metadata) {
    D3D12_RESOURCE_DESC desc{};
    desc.Width = UINT(metadata.width);
    desc.Height = UINT(metadata.height);
    desc.MipLevels = UINT16(metadata.mipLevels);
    desc.DepthOrArraySize = UINT16(metadata.arraySize);
    desc.Format = metadata.format;
    desc.SampleDesc.Count = 1;
    desc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    ComPtr<ID3D12Resource> resource;

    [[maybe_unused]] HRESULT hr = device->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&resource));

    assert(SUCCEEDED(hr));
    return resource;
}

ComPtr<ID3D12Resource> TextureManager::UploadTextureData(
    ID3D12Resource* texture,
    const DirectX::ScratchImage& mipImages,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList) {
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;

    PrepareUpload(
        device,
        mipImages.GetImages(),
        mipImages.GetImageCount(),
        mipImages.GetMetadata(),
        subresources);

    uint64_t intermediateSize =
        GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));

    ComPtr<ID3D12Resource> intermediateResource =
        CreateBufferResource(device, intermediateSize);

    UpdateSubresources(
        commandList,
        texture,
        intermediateResource.Get(),
        0, 0,
        UINT(subresources.size()),
        subresources.data());

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;

    commandList->ResourceBarrier(1, &barrier);

    return intermediateResource;
}
