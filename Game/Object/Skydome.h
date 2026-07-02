#pragma once

#include <cstdint>
#include <d3d12.h>

#include "Engine/Math/Vector3.h"
#include "Engine/Math/Matrix4x4.h"
#include "Engine/Rendering/ConstantBuffer.h"
#include "Engine/Rendering/Material.h"
#include "Engine/Rendering/Mesh.h"
#include "Engine/Rendering/TransformationMatrix.h"

/// <summary>
/// 天球。内側から見る大きな球で、シーンの背景として最初に描画する。
/// ・ライティング無効（テクスチャの色をそのまま表示）
/// ・内側から見えるよう、カリング無効のPSO（PipelineManagerのkNoCull）を使う
/// ・ImGuiでカメラ追従のON/OFFを切り替え（ON:中心をカメラ位置へ追従 / OFF:原点固定）
/// </summary>
class Skydome {
public:
    /// <summary>
    /// モデル・テクスチャ・マテリアル等を初期化する。
    /// （PipelineManager・TextureManagerの初期化後に呼ぶ）
    /// </summary>
    /// <param name="device">リソース生成に使うデバイス</param>
    void Initialize(ID3D12Device* device);

    /// <summary>
    /// カメラのビュー行列から追従位置を決め、WVPを更新する。毎フレーム1回呼ぶ。
    /// </summary>
    void Update(const Matrix4x4& view, const Matrix4x4& projection);

    /// <summary>
    /// カリング無効PSOに切り替えて天球を描画する（背景なので他オブジェクトより先に呼ぶ）。
    /// </summary>
    /// <param name="lightAddress">共通ルートシグネチャが要求するため設定する平行光源のCBVアドレス（ライティングは無効）</param>
    void Draw(
        ID3D12GraphicsCommandList* commandList,
        D3D12_GPU_VIRTUAL_ADDRESS lightAddress);

#ifdef USE_IMGUI
    // カメラ追従・スケール・原点固定時の位置を調整するUI
    void DrawImGui();
#endif

private:
    // --- モデル（resources/skydome.obj、半径1のユニット球）---
    Mesh mesh_;

    // --- テクスチャ（resources/sky_sphere.png）---
    uint32_t textureHandle_ = 0;

    // --- マテリアル（ライティング無効）とTransform ---
    Material material_{};
    ConstantBuffer<Material> materialCB_;
    ConstantBuffer<TransformationMatrix> transformCB_;

    // --- 調整用パラメータ ---
    Vector3 position_{ 0.0f, 0.0f, 0.0f };  // 原点固定時の中心位置
    float scale_ = 50.0f;                   // 半径（far=100に収まるよう既定50）
    bool followCamera_ = false;             // ON:中心をカメラ位置へ追従
};
