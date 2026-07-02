#pragma once

#include <memory>

#include "Engine/Core/Framework.h"
#include "Engine/Graphics/CameraCapture.h"
#include "Engine/Input/EyeTracker.h"
#include "Engine/Input/FaceTracker.h"
#include "Game/Scene/GameScene.h"

/// <summary>
/// このゲームのアプリケーションクラス。
/// Frameworkのメインループから呼ばれる更新・描画をGameSceneへ委譲し、
/// 視線追跡（アプリ内顔検出／共有メモリ）とWebカメラの分割表示をシーンへ配線する。
/// </summary>
class MyGame : public Framework {
protected:

    void Initialize() override;

    void Finalize() override;

    void Update() override;

    void Draw(ID3D12GraphicsCommandList* commandList, uint32_t viewIndex) override;

    // Webカメラ表示ON時、最新フレームをGPUテクスチャへ転送する
    void PreDraw(ID3D12GraphicsCommandList* commandList) override;

    // 画面分割時の右半分にWebカメラ映像を描く
    void DrawSubView(
        ID3D12GraphicsCommandList* commandList,
        const D3D12_VIEWPORT& viewport,
        const D3D12_RECT& scissorRect) override;

#ifdef USE_IMGUI
    void DrawImGui() override;
#endif

private:

    // シーン（Finalizeでエンジン終了処理より先に解放する）
    std::unique_ptr<GameScene> scene_;

    // --- 視線追跡とWebカメラ ---
    CameraCapture camera_;    // Webカメラ取得・表示（使わない間はワーカー停止）
    FaceTracker faceTracker_; // アプリ内顔検出（Webカメラのフレームから頭位置を推定）
    EyeTracker eyeTracker_;   // 外部プロセスから共有メモリ経由で視線を受信（差し替え用に残置）

    bool useEyeTracking_ = false;  // 視線追跡でゲーム内カメラを連動させる（ImGuiでON/OFF）
    bool showCamera_ = false;      // 実カメラを画面右半分に映す（ON時は画面分割）
    int gazeSource_ = 0;           // 視線の取得元（0=アプリ内顔検出 / 1=共有メモリ）
};
