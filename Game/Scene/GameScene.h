#pragma once

#include <d3d12.h>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <dxcapi.h>
#include <wrl.h>

#include "TransformData3D.h"
#include "LoadObjFile.h"
#include "RenderResource.h"
#include "FrustumCulling.h"
#include "Engine/Object/Skydome.h"
// デバッグカメラはDebugビルド限定。このプロジェクトはReleaseでも_DEBUGが定義される
// （RuntimeLibrary=MultiThreadedDebug）ため、Release判定にはNDEBUGを使う。
#ifndef NDEBUG
#include "Engine/Camera/DebugCamera.h"
#endif

struct VertexData;

// デモ用シーン。三角形・球・OBJ・スプライトと開発用ImGuiを保持する。
class GameScene {
public:
    // device:リソース生成 / rootSignature・各シェーダー:天球の専用PSO生成に使用
    // viewCount:立体視の視点数（視点ごとのビュー射影CBufferを確保する）
    void Initialize(
        ID3D12Device* device,
        ID3D12RootSignature* rootSignature,
        IDxcBlob* vertexShader,
        IDxcBlob* pixelShader,
        uint32_t viewCount);

    // UI操作を反映した行列計算と定数バッファ更新
    void Update();

    // 視線追跡の状態を反映する（Updateの前に毎フレーム呼ぶ）。
    // enabled:視線追跡ON / gazeX,gazeY:正規化視線[-1..1]（-1左下 +1右上） / headZ:奥行き[-1..1]
    // enabledかつ値が非ゼロのとき、頭連動オフアクシス投影でカメラ視点をずらす。
    void SetEyeTracking(bool enabled, float gazeX, float gazeY, float headZ) {
        eyeTrackingEnabled_ = enabled;
        gazeX_ = gazeX;
        gazeY_ = gazeY;
        gazeZ_ = headZ;
    }

    // 描画先のアスペクト補正（Updateの前に毎フレーム呼ぶ）。
    // ゲーム描画が表示される横方向の割合を渡す（通常=1.0 / 左右分割でゲームが左半分のとき=0.5）。
    // 投影のアスペクト比をこの割合ぶん横に詰め、分割しても物体が伸び縮みしないようにする。
    void SetRenderAspectScale(float horizontalScale) { renderAspectScale_ = horizontalScale; }

#ifdef USE_IMGUI
    // 開発用ImGuiウィンドウの構築（deviceは球の再分割時の再生成に使用）
    void DrawImGui(ID3D12Device* device);
#endif

    // 描画コマンドを積む。textureHandlesは読み込み済みテクスチャのSRV(GPUハンドル)配列。
    // viewIndex:描画する視点（この視点のビュー射影CBufferをVSへバインドする）。視点数ぶん呼ぶ。
    void Draw(
        ID3D12GraphicsCommandList* commandList,
        ID3D12RootSignature* rootSignature,
        ID3D12PipelineState* pipelineState,
        ID3D12DescriptorHeap* srvDescriptorHeap,
        const D3D12_GPU_DESCRIPTOR_HANDLE* textureHandles,
        uint32_t viewIndex);

private:
    // --- 三角形 ---
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceTriangle_;
    VertexData* vertexDataTriangle_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vbvTriangle_{};

    // --- OBJ ---
    ModelData modelData_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceObj_;
    D3D12_VERTEX_BUFFER_VIEW vbvObj_{};

    // --- 天球（背景）---
    Skydome skydome_;

    // --- 球 ---
    uint32_t subdivision_ = 16;
    uint32_t prevSubdivision_ = 16;
    uint32_t sphereVertexCount_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSphere_;
    D3D12_VERTEX_BUFFER_VIEW vbvSphere_{};

    // --- スプライト ---
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSprite_;
    VertexData* vertexDataSprite_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vbvSprite_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSprite_;
    uint32_t* indexDataSprite_ = nullptr;
    D3D12_INDEX_BUFFER_VIEW ibvSprite_{};

    // --- マテリアル / ライト / Transformリソース ---
    MaterialResource material_;        // 三角形・OBJ共通の3Dマテリアル
    MaterialResource spriteMaterial_;  // スプライト用（ライティング無効）
    DirectionalLightResource light_;
    TransformResource triangleTransform_;
    TransformResource sphereTransform_;
    TransformResource objTransform_;
    TransformResource spriteTransform_;

    // --- 立体視：視点ごとのビュー射影CBuffer（viewCountぶん）＋スプライト用の正射影CBuffer ---
    uint32_t viewCount_ = 1;
    std::vector<ViewProjectionResource> viewProjections_;  // [viewIndex] 3D用のビュー射影（眼ごと）
    ViewProjectionResource spriteViewProjection_;          // スプライト（2D）用の正射影（視点非依存）

    // 共有ステレオパラメータ（ImGuiで調整）。視点描画に効く。
    float eyeSeparation_ = 0.1f;   // 眼間距離（ワールド単位）
    float convergence_ = 10.0f;    // 収束距離（視差ゼロ面までの距離。カメラ→被写体の距離が目安）

    // --- 視線追跡（頭連動オフアクシス）---
    // 視線位置を擬似的な頭位置とみなし、カメラを左右/上下へ平行移動＋射影を水平/垂直シアーする
    // （収束面を固定したまま「窓の中を覗き込む」3DS風の運動視差。物体を立体的な角度から見やすくする）。
    bool eyeTrackingEnabled_ = false;
    float gazeX_ = 0.0f;  // 正規化視線（-1左/+1右）
    float gazeY_ = 0.0f;  // 正規化視線（-1下/+1上）
    float gazeZ_ = 0.0f;  // 正規化奥行き（-1後/+1前。未使用）
    float gazeMoveScaleX_ = 1.2f;  // 視線が端のときの水平移動量（ワールド単位）
    float gazeMoveScaleY_ = 0.7f;  // 視線が端のときの垂直移動量（ワールド単位）

    // 描画先の横方向の割合（1.0=画面全体 / 0.5=左右分割でゲームが左半分）。投影アスペクト補正に使う。
    float renderAspectScale_ = 1.0f;

    // --- CPU側Transform ---
    Transform3D transformTriangle_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {2.5f, 0.0f, 0.0f} };
    Transform3D transformSphere_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform3D transformObj_{ {1.0f, 1.0f, 1.0f}, {0.0f, 3.1415f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform3D transformSprite_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform3D uvTransformSprite_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform3D cameraTransform_{ {1.0f, 1.0f, 1.0f}, {0.04f, 0.0f, 0.0f}, {0.0f, 1.7f, -10.0f} };

    // --- 使用するテクスチャの選択（textureHandlesへのindex）---
    uint32_t triangleTextureIndex_ = 0;
    uint32_t sphereTextureIndex_ = 1;
    uint32_t objTextureIndex_ = 0;
    uint32_t spriteTextureIndex_ = 0;
    uint32_t skydomeTextureIndex_ = 2;  // textureHandles[2] = sky_sphere.png

    // --- サウンド ---
    size_t soundHandle_ = 0;     // Alarm01.wavのハンドル
    float soundVolume_ = 1.0f;   // ImGuiで調整する音量

    // --- スプライト描画のオン/オフ（ImGuiで切り替え） ---
    bool drawSprite_ = true;

    // --- 視錐台カリング ---
    // 三角形・OBJのローカル空間バウンディング球（Initializeで頂点から算出。球オブジェクトは半径1のユニット球）。
    // カリングとデバッグカメラのピッキングで共用するため、Releaseでも保持する。
    Vector3 localCenterTriangle_{ 0.0f, 0.0f, 0.0f };
    float localRadiusTriangle_ = 0.0f;
    Vector3 localCenterObj_{ 0.0f, 0.0f, 0.0f };
    float localRadiusObj_ = 0.0f;
    // 各オブジェクトのカリング判定結果（Updateで更新し、Drawで参照する）
    FrustumVisibility triangleVisibility_ = FrustumVisibility::Inside;
    FrustumVisibility sphereVisibility_ = FrustumVisibility::Inside;
    FrustumVisibility objVisibility_ = FrustumVisibility::Inside;
    FrustumVisibility spriteVisibility_ = FrustumVisibility::Inside;

#ifndef NDEBUG
    // --- デバッグカメラ（Debugビルドのみ。Releaseでは無効）---
    DebugCamera debugCamera_;
#endif
};
