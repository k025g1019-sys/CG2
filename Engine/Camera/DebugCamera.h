#pragma once

#include <vector>

#include "Vector3.h"
#include "Matrix4x4.h"

/// <summary>
/// マウス操作で対象を見回す開発用カメラ（ターンテーブル方式のオービット）。
/// ・左クリック : カーソル下のオブジェクトを選択し、その中心を注視点にする
/// ・左/右ドラッグ : 注視点を中心にピボット回転（右ドラッグはピッキングしない）
/// ・ホイール : 注視点に対して寄り引き（ズーム）
/// ・Enter : 有効・無効をトグル
/// </summary>
class DebugCamera {
public:
    /// <summary>
    /// ピッキング対象。ワールド空間のバウンディング球で表す。
    /// </summary>
    struct PickTarget {
        Vector3 center;  // 球の中心（ワールド空間）
        float radius;    // 球の半径（ワールド空間）
    };

    /// <summary>
    /// 入力を反映してカメラを更新する。毎フレーム1回呼ぶ。
    /// </summary>
    /// <param name="targets">ピッキング対象のバウンディング球リスト</param>
    /// <param name="screenWidth">クライアント領域の幅（ピクセル）</param>
    /// <param name="screenHeight">クライアント領域の高さ（ピクセル）</param>
    /// <param name="projection">ピッキングのレイ生成に使う透視投影行列</param>
    /// <param name="blockMouse">ImGui等がマウスを使用中はtrue。マウス操作を無視する</param>
    void Update(
        const std::vector<PickTarget>& targets,
        float screenWidth,
        float screenHeight,
        const Matrix4x4& projection,
        bool blockMouse);

    // 有効・無効状態
    bool IsEnabled() const { return enabled_; }

    // 現在のオービット状態から計算したビュー行列
    Matrix4x4 GetViewMatrix() const;

#ifdef USE_IMGUI
    // 状態表示と各パラメータの調整UI
    void DrawImGui();
#endif

private:
    // 現在のオービット状態からカメラのワールド行列を作る
    Matrix4x4 MakeCameraWorldMatrix() const;

    // --- オービット状態 ---
    Vector3 target_ = { 0.0f, 0.0f, 0.0f };  // 注視点（最後に選択したオブジェクトの中心）
    float distance_ = 10.0f;                 // 注視点からの距離（ホイールで増減）
    float yaw_ = 0.0f;                        // ワールドY軸まわりの回転角
    float pitch_ = 0.1f;                      // カメラ右軸まわりの回転角

    bool enabled_ = false;                    // Enterでトグルする有効フラグ

    // --- 調整用パラメータ ---
    float rotateSpeed_ = 0.005f;   // ドラッグ1ピクセルあたりの回転量（rad）
    float zoomSpeed_ = 0.01f;      // ホイール1目盛りあたりのズーム量
    float minDistance_ = 1.0f;     // 寄りの限界
    float maxDistance_ = 100.0f;   // 引きの限界
};
