#pragma once

#include <d3d12.h>
#include <cstdint>
#include <wrl.h>

struct Matrix4x4;
struct Transform3D;
struct Material;
struct DirectionalLight;
struct TransformationMatrix;

// オブジェクト描画に使う定数バッファ群と描画ヘルパ。
// resourceはComPtrで自動開放。dataはMap済みの書き込み先ポインタ（resource生存中のみ有効）。

#pragma region TransformationMatrix（World）

struct TransformResource {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    TransformationMatrix* data = nullptr;
};

TransformResource CreateTransformResource(ID3D12Device* device);

// Transformからワールド行列を計算してTransformResourceへ書き込む（視点に依存しない）
void UpdateTransformMatrix(
    TransformResource& transformResource,
    const Transform3D& transform);

#pragma endregion


#pragma region ViewProjection（視点ごとのビュー射影行列）

struct ViewProjectionResource {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    Matrix4x4* data = nullptr;
};

ViewProjectionResource CreateViewProjectionResource(ID3D12Device* device);

// ビュー射影行列をViewProjectionResourceへ書き込む
void UpdateViewProjection(
    ViewProjectionResource& viewProjectionResource,
    const Matrix4x4& viewProjection);

#pragma endregion


#pragma region Material

struct MaterialResource {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    Material* data = nullptr;
};

MaterialResource CreateMaterialResource(ID3D12Device* device, bool enableLighting);

#pragma endregion


#pragma region DirectionalLight

struct DirectionalLightResource {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    DirectionalLight* data = nullptr;
};

DirectionalLightResource CreateDirectionalLight(ID3D12Device* device);

#pragma endregion


#pragma region 描画

void DrawObject(
    ID3D12GraphicsCommandList* commandList,
    D3D12_VERTEX_BUFFER_VIEW& vbv,
    D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
    ID3D12Resource* transformResource,
    uint32_t vertexCount);

#pragma endregion
