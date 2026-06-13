#include "TextureManager.h"
#include <cassert>
#include <wrl.h>
#include "ConvertString.h"
#include "GpuResource.h"
#include "externals/DirectXTex/d3dx12.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

TextureData TextureManager::LoadAndUpload(
    const std::string& filepath,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList) {
    TextureData texture = LoadTexture(filepath);
    texture.textureResource = CreateTextureResource(device, texture.metadata);
    texture.intermediateResource = UploadTextureData(
        texture.textureResource.Get(),
        texture.mipImage,
        device,
        commandList);
    return texture;
}

TextureData TextureManager::LoadTexture(const std::string& filepath) {
    TextureData data;

    std::wstring filePathW = ConvertString(filepath);

    ScratchImage image;
    HRESULT hr = LoadFromWICFile(
        filePathW.c_str(),
        WIC_FLAGS_FORCE_SRGB,
        nullptr,
        image);
    assert(SUCCEEDED(hr));

    hr = GenerateMipMaps(
        image.GetImages(),
        image.GetImageCount(),
        image.GetMetadata(),
        TEX_FILTER_SRGB,
        0,
        data.mipImage);
    assert(SUCCEEDED(hr));

    data.metadata = data.mipImage.GetMetadata();
    return data;
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

    HRESULT hr = device->CreateCommittedResource(
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