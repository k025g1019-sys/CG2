#pragma once

#include <d3d12.h>
#include <string>

struct Matrix4x4;
struct Transform3D;
struct Material;
struct DirectionalLight;
struct TransformationMatrix;
struct TextureData;

#pragma region Resource作成

ID3D12Resource* CreateBufferResource(
    ID3D12Device* device,
    size_t sizeInBytes);

ID3D12Resource* CreateDepthStencilTextureResource(
    ID3D12Device* device,
    int32_t width,
    int32_t height);

#pragma endregion


#pragma region TransformationMatrix生成

struct TransformResource {
    ID3D12Resource* resource = nullptr;
    TransformationMatrix* data = nullptr;
};

TransformResource CreateTransformResource(
    ID3D12Device* device);

#pragma endregion


#pragma region Material生成

struct MaterialResource {
    ID3D12Resource* resource = nullptr;
    Material* data = nullptr;
};

MaterialResource CreateMaterialResource(
    ID3D12Device* device,
    bool enableLighting);

#pragma endregion


#pragma region DirectionalLight生成

struct DirectionalLightResource {
    ID3D12Resource* resource = nullptr;
    DirectionalLight* data = nullptr;
};

DirectionalLightResource CreateDirectionalLight(
    ID3D12Device* device);

#pragma endregion


#pragma region Textureロード

TextureData LoadTextureAndUpload(
    const std::string& path,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList);

#pragma endregion


#pragma region 行列更新

void UpdateTransformMatrix(
    TransformResource& transformResource,
    const Transform3D& transform,
    const Matrix4x4& view,
    const Matrix4x4& projection);

#pragma endregion


#pragma region DrawObject

void DrawObject(
    ID3D12GraphicsCommandList* commandList,
    D3D12_VERTEX_BUFFER_VIEW& vbv,
    D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
    ID3D12Resource* transformResource,
    uint32_t vertexCount);

#pragma endregion