#pragma once

#include <cstddef>
#include <cstdint>
#include <d3d12.h>

#include "Engine/Camera/Camera.h"
#include "Engine/Light/DirectionalLight.h"
#include "Game/Object/Skydome.h"
#include "Engine/Rendering/ConstantBuffer.h"
#include "Engine/Rendering/Mesh.h"
#include "Engine/Rendering/Object3D.h"
#include "Engine/Rendering/Sprite.h"
// デバッグカメラはDebugビルド限定。このプロジェクトはReleaseでも_DEBUGが定義される
// （RuntimeLibrary=MultiThreadedDebug）ため、Release判定にはNDEBUGを使う。
#ifndef NDEBUG
#include "Engine/Camera/DebugCamera.h"
#endif

// デモ用シーン。三角形・球・OBJ・スプライト・天球と開発用ImGuiを保持する。
class GameScene {
public:
    // 各リソースを生成する（DirectXCore・PipelineManager・TextureManagerの初期化後に呼ぶ）
    void Initialize();

    // UI操作を反映した行列計算・定数バッファ更新・カリング判定。
    // 立体視が有効なときは視点ごとのビュー射影（平行配置＋オフアクシス射影）もここで更新する。
    void Update();

    // 視線追跡の状態を反映する（Updateの前に毎フレーム呼ぶ）。
    // enabled:視線追跡ON / gazeX,gazeY:正規化視線[-1..1]（-1左下 +1右上） / headZ:奥行き[-1..1]
    // enabledのとき、頭連動オフアクシス投影でカメラ視点をずらす（立体視OFFでも運動視差として効く）。
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
    // 開発用ImGuiウィンドウの構築
    void DrawImGui();
#endif

    // 描画コマンドを積む。
    // viewIndex:描画する視点（この視点のビュー射影CBufferをVS[b1]へバインドする）。
    // 立体視有効時はStereoRendererの視点数ぶん呼ばれ、無効時はviewIndex=0で1回呼ばれる。
    void Draw(ID3D12GraphicsCommandList* commandList, uint32_t viewIndex);

private:
    // --- メッシュ（形状データ）---
    Mesh triangleMesh_;
    Mesh sphereMesh_;
    Mesh objMesh_;

    // --- 描画オブジェクト ---
    Object3D triangle_;
    Object3D sphere_;
    Object3D obj_;
    Sprite sprite_;
    Skydome skydome_;  // 背景（最初に描画）

    // --- カメラ ---
    Camera camera_;

    // --- 立体視：視点ごとのビュー射影CBuffer ---
    // スロットは [フレームスロット][視点] の2次元（index = frame * kMaxViewCount + view）。
    ConstantBuffer<Matrix4x4> viewProjectionCB_;

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

    // --- 平行光源（CPU側の値をImGuiで編集し、Updateで定数バッファへ書き込む）---
    DirectionalLight light_{ { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, -1.0f, 0.0f }, 1.0f };
    ConstantBuffer<DirectionalLight> lightCB_;

    // --- 球の分割数（ImGuiで変更すると頂点を再生成する）---
    uint32_t subdivision_ = 16;
    uint32_t prevSubdivision_ = 16;

    // --- シーンで使うテクスチャ（TextureManagerのハンドル。ImGuiのComboに対応）---
    uint32_t textureHandles_[2] = {};  // 0:uvChecker / 1:monsterBall
    int triangleTextureIndex_ = 0;
    int sphereTextureIndex_ = 1;
    int objTextureIndex_ = 0;
    int spriteTextureIndex_ = 0;

    // --- サウンド ---
    size_t soundHandle_ = 0;     // Alarm01.wavのハンドル
    float soundVolume_ = 1.0f;   // ImGuiで調整する音量

    // --- スプライト描画のオン/オフ（ImGuiで切り替え） ---
    bool drawSprite_ = true;

#ifndef NDEBUG
    // --- デバッグカメラ（Debugビルドのみ。Releaseでは無効）---
    DebugCamera debugCamera_;
#endif
};
