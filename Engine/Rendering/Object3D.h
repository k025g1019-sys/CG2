#pragma once

#include <cstdint>
#include <d3d12.h>

#include "Engine/Culling/FrustumCulling.h"
#include "Engine/Math/Matrix4x4.h"
#include "Engine/Rendering/ConstantBuffer.h"
#include "Engine/Rendering/Material.h"
#include "Engine/Rendering/TransformData3D.h"
#include "Engine/Rendering/TransformationMatrix.h"

class Mesh;

/// <summary>
/// メッシュ＋Transform＋マテリアル＋テクスチャを組にした3D描画オブジェクト。
/// Update: 行列計算・定数バッファ書き込み・視錐台カリング判定
/// Draw:   カリング結果がOutsideでなければ描画コマンドを積む
/// </summary>
class Object3D {
public:

    /// <param name="mesh">形状（非所有。呼び出し側が生存期間を管理する）</param>
    /// <param name="textureHandle">TextureManagerのテクスチャハンドル</param>
    /// <param name="enableLighting">平行光源によるライティングを行うか</param>
    void Initialize(ID3D12Device* device, Mesh* mesh, uint32_t textureHandle, bool enableLighting = true);

    // 行列計算・定数バッファ更新・視錐台カリング判定（毎フレーム呼ぶ）
    void Update(const Matrix4x4& view, const Matrix4x4& projection, const Frustum3D& frustum);

    // カリング結果がOutsideでなければ描画する
    // （RootSignature・PSO・トポロジ・ライトCBVは呼び出し側で設定済みの前提）
    void Draw(ID3D12GraphicsCommandList* commandList) const;

    // 現在のTransformとメッシュから、ワールド空間のバウンディング球を計算する（ピッキング用）
    Sphere CalcWorldBoundingSphere() const;

    Transform3D& GetTransform() { return transform_; }
    const Transform3D& GetTransform() const { return transform_; }

    // CPU側マテリアル（ImGuiで色・ライティングを編集し、Updateで定数バッファへ反映される）
    Material& GetMaterial() { return material_; }

    void SetTextureHandle(uint32_t textureHandle) { textureHandle_ = textureHandle; }
    uint32_t GetTextureHandle() const { return textureHandle_; }

    FrustumVisibility GetVisibility() const { return visibility_; }

    Mesh* GetMesh() const { return mesh_; }

private:

    Mesh* mesh_ = nullptr;  // 非所有

    Transform3D transform_{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

    Material material_{};   // CPU側の値。Updateで定数バッファへ書き込む

    uint32_t textureHandle_ = 0;

    ConstantBuffer<TransformationMatrix> transformCB_;

    ConstantBuffer<Material> materialCB_;

    // Updateで判定したカリング結果（Drawで参照する）
    FrustumVisibility visibility_ = FrustumVisibility::Inside;
};
