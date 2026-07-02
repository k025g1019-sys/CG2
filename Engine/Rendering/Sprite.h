#pragma once

#include <cstdint>
#include <d3d12.h>

#include "Engine/Culling/FrustumCulling.h"
#include "Engine/Math/Vector2.h"
#include "Engine/Rendering/ConstantBuffer.h"
#include "Engine/Rendering/Material.h"
#include "Engine/Rendering/Mesh.h"
#include "Engine/Rendering/TransformData3D.h"
#include "Engine/Rendering/TransformationMatrix.h"

/// <summary>
/// 2Dスプライト。スクリーン座標系（左上原点・ピクセル単位）のクアッドを
/// 正射影で描画する。ライティングは無効。
/// Updateで画面矩形との2Dカリングも判定し、画面外なら描画をスキップする。
/// </summary>
class Sprite {
public:

    /// <param name="textureHandle">TextureManagerのテクスチャハンドル</param>
    /// <param name="size">スプライトのサイズ（ピクセル）</param>
    void Initialize(ID3D12Device* device, uint32_t textureHandle, const Vector2& size);

    // ワールド行列・正射影・マテリアルの定数バッファ更新と、画面矩形との2Dカリング判定（毎フレーム呼ぶ）
    void Update(float screenWidth, float screenHeight);

    // カリング結果がOutsideでなければ描画する。
    // 2Dオーバーレイなので、ビュー射影（VSのb1）は自前の正射影へ差し替えて描く
    // （立体視でも両眼で同一＝視差ゼロで表示される）。
    // （RootSignature・PSO・トポロジは呼び出し側で設定済みの前提）
    void Draw(ID3D12GraphicsCommandList* commandList) const;

    Transform3D& GetTransform() { return transform_; }

    // UV変換（scale / rotate.z / translateを使用）
    Transform3D& GetUVTransform() { return uvTransform_; }

    // CPU側マテリアル（ImGuiで編集し、Updateで定数バッファへ反映される）
    Material& GetMaterial() { return material_; }

    // Map済み頂点への書き込みアクセス（ImGuiでの頂点編集用。4頂点）
    VertexData* GetMappedVertices() { return mesh_.GetMappedVertices(); }

    void SetTextureHandle(uint32_t textureHandle) { textureHandle_ = textureHandle; }

    FrustumVisibility GetVisibility() const { return visibility_; }

private:

    Mesh mesh_;  // クアッド（4頂点＋6インデックス）

    Transform3D transform_{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

    Transform3D uvTransform_{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

    Material material_{};  // ライティング無効で初期化される

    uint32_t textureHandle_ = 0;

    ConstantBuffer<TransformationMatrix> transformCB_;

    ConstantBuffer<Material> materialCB_;

    // スプライト用の正射影（視点非依存。VSのb1へ自前でバインドする）
    ConstantBuffer<Matrix4x4> viewProjectionCB_;

    // Updateで判定した2Dカリング結果（Drawで参照する）
    FrustumVisibility visibility_ = FrustumVisibility::Inside;
};
