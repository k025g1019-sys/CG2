#pragma once

#include "Engine/Math/Matrix4x4.h"
#include "Engine/Rendering/TransformData3D.h"

/// <summary>
/// シーンを映すカメラ。Transformと投影パラメータから
/// ビュー行列・プロジェクション行列を作る。
/// </summary>
class Camera {
public:

    // Transformのワールド行列の逆行列＝ビュー行列
    Matrix4x4 GetViewMatrix() const;

    // 透視投影行列（アスペクト比は画面サイズに追従させるため毎回渡す）
    Matrix4x4 GetProjectionMatrix(float aspectRatio) const;

    Transform3D& GetTransform() { return transform_; }
    const Transform3D& GetTransform() const { return transform_; }

    void SetFovY(float fovY) { fovY_ = fovY; }
    float GetFovY() const { return fovY_; }

    void SetClipPlanes(float nearClip, float farClip) {
        nearClip_ = nearClip;
        farClip_ = farClip;
    }
    float GetNearClip() const { return nearClip_; }
    float GetFarClip() const { return farClip_; }

private:

    Transform3D transform_{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

    float fovY_ = 0.45f;      // 垂直視野角（ラジアン）

    float nearClip_ = 0.1f;   // ニアクリップ距離

    float farClip_ = 100.0f;  // ファークリップ距離
};
