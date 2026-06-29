#pragma once

#include <cstdint>
#include <atomic>
#include <memory>

// 内蔵カメラのフレーム（CameraCaptureのワーカースレッドから供給されるBGRA32）に対して
// Windows標準の顔検出（Windows.Media.FaceAnalysis.FaceDetector）をかけ、顔（頭）の画面内位置を
// 正規化視線値として提供する。共有メモリ経由のEyeTrackerと同じ出力形（GetGazeX/Y/Z等）を持ち、
// アプリ単体で「顔の動き → ゲーム内カメラ」を完結させる（外部プロセスや共有メモリは不要）。
//
// スレッド：ProcessFrameBGRA()はカメラのワーカースレッドから、Update()/getterはメインスレッドから
// 呼ばれる前提。生の検出結果はatomicで受け渡し、平滑化はUpdate()（メインスレッド）で行う。
// WinRT本体はpimplに隠し、ヘッダにwinrt/Windows.hを露出させない。
class FaceTracker {
public:
    FaceTracker();
    ~FaceTracker();

    // 軽量な初期化。WinRTトラッカー本体は最初のProcessFrameBGRA()でワーカースレッド上に遅延生成する。
    void Initialize();

    // WinRTトラッカーを解放する。必ずカメラのワーカースレッド停止後（CameraCapture::Finalize後）に呼ぶ。
    void Finalize();

    // カメラのワーカースレッドから毎フレーム呼ぶ（内部で時間間引きするので毎フレーム渡してよい）。
    // bgra:top-down・隙間なしのBGRA32 / w,h:画素数。
    void ProcessFrameBGRA(const uint8_t* bgra, int w, int h);

    // メインスレッドで毎フレーム呼ぶ。生の検出結果を平滑化する（未検出が続くと中央へ戻す）。
    void Update();

    // 直近で顔を検出できているか（未検出が一定回数続くとfalse）。
    bool IsConnected() const { return connected_; }

    // 平滑化済みの正規化視線位置。
    float GetGazeX() const { return smoothX_; }  // [-1(左)..+1(右)]
    float GetGazeY() const { return smoothY_; }  // [-1(下)..+1(上)]
    float GetHeadZ() const { return smoothZ_; }  // [-1(後)..+1(前)]

    // 平滑化係数（0..1）。大きいほど追従が速い／小さいほど滑らか。ImGuiで調整する。
    void SetSmoothing(float alpha) { smoothing_ = alpha; }
    float GetSmoothing() const { return smoothing_; }

    // --- 診断用（ImGui表示で接続トラブルを切り分ける）---
    bool IsReady() const { return ready_.load(); }          // WinRTトラッカーを生成できたか
    bool IsFaceValid() const { return rawValid_.load(); }   // 直近フレームで顔を検出したか
    int GetFaceCount() const { return faceCount_.load(); }  // 直近フレームの検出顔数

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;  // WinRTトラッカー＋スクラッチ（cppに隠す）

    // --- ワーカースレッド → メインスレッド（atomicで受け渡し）---
    std::atomic<float> rawX_{ 0.0f };
    std::atomic<float> rawY_{ 0.0f };
    std::atomic<float> rawZ_{ 0.0f };
    std::atomic<bool>  rawValid_{ false };
    std::atomic<int>   faceCount_{ 0 };
    std::atomic<bool>  ready_{ false };

    // --- メインスレッドのみが触る平滑化状態 ---
    bool connected_ = false;
    float smoothX_ = 0.0f;
    float smoothY_ = 0.0f;
    float smoothZ_ = 0.0f;
    float smoothing_ = 0.25f;
    int staleCount_ = 0;  // 顔未検出が続いた回数

    // 顔の移動 → 正規化視線値の感度（送信側gaze_sender.pyの既定1.6に合わせる）。
    float gain_ = 1.6f;
};
