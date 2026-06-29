#pragma once

#include <cstdint>
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>

#include "Vector3.h"
#include "Matrix4x4.h"
#include "TransformData3D.h"
#include "LoadObjFile.h"
#include "RenderResource.h"

/// <summary>
/// 天球。内側から見る大きな球で、シーンの背景として最初に描画する。
/// ・ライティング無効（テクスチャの色をそのまま表示）
/// ・内側から見えるよう、カリングを無効化した専用PSOを使う
/// ・ImGuiでカメラ追従のON/OFFを切り替え（ON:中心をカメラ位置へ追従 / OFF:原点固定）
/// </summary>
class Skydome {
public:
    /// <summary>
    /// 天球を初期化する。
    /// </summary>
    /// <param name="device">リソース生成に使うデバイス</param>
    /// <param name="rootSignature">専用PSO生成に使う共通ルートシグネチャ</param>
    /// <param name="vertexShader">専用PSOに使う標準頂点シェーダー</param>
    /// <param name="pixelShader">専用PSOに使う標準ピクセルシェーダー</param>
    void Initialize(
        ID3D12Device* device,
        ID3D12RootSignature* rootSignature,
        IDxcBlob* vertexShader,
        IDxcBlob* pixelShader);

    /// <summary>
    /// カメラのビュー行列から追従位置を決め、ワールド行列を更新する。毎フレーム1回呼ぶ。
    /// ビュー射影は視点ごとに別CBufferで供給するため、ここでは扱わない（中心カメラのビューのみ使う）。
    /// </summary>
    void Update(const Matrix4x4& centerView);

    /// <summary>
    /// 専用PSOに切り替えて天球を描画する（背景なので他オブジェクトより先に呼ぶ）。
    /// ルートシグネチャと視点ごとのビュー射影（b1[VS]）は呼び出し側で設定済みの前提。
    /// </summary>
    /// <param name="textureHandle">天球テクスチャのSRV(GPUハンドル)</param>
    /// <param name="lightResource">共通ルートsignが要求するため設定する平行光源（ライティングは無効）</param>
    void Draw(
        ID3D12GraphicsCommandList* commandList,
        D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
        ID3D12Resource* lightResource);

#ifdef USE_IMGUI
    // カメラ追従・スケール・原点固定時の位置を調整するUI
    void DrawImGui();
#endif

private:
    // --- モデル（resources/skydome.obj、半径1のユニット球）---
    ModelData modelData_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vbv_{};
    uint32_t vertexCount_ = 0;

    // --- マテリアル（ライティング無効）---
    MaterialResource material_;

    // --- Transform（World。ビュー射影は視点ごとに別CBufferで供給）---
    TransformResource transform_;

    // --- 内側から見えるようカリングを無効化した専用PSO ---
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    // --- 調整用パラメータ ---
    Vector3 position_{ 0.0f, 0.0f, 0.0f };  // 原点固定時の中心位置
    float scale_ = 50.0f;                   // 半径（far=100に収まるよう既定50）
    bool followCamera_ = false;             // ON:中心をカメラ位置へ追従
};
