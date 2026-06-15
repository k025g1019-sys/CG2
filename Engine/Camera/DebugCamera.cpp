#include "DebugCamera.h"

#include <cfloat>
#include <cmath>

#include "Engine/Input/Input.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

namespace {
// レイと球の交差判定。交差すればtrueを返し、tに交差点までの距離を入れる。
bool IntersectRaySphere(
    const Vector3& origin,
    const Vector3& direction,
    const Vector3& center,
    float radius,
    float& t) {
    Vector3 m = origin - center;
    float b = Dot(m, direction);
    float c = Dot(m, m) - radius * radius;
    // 始点が球の外側にあり、かつ球から遠ざかる向きなら交差しない
    if (c > 0.0f && b > 0.0f) {
        return false;
    }
    float discriminant = b * b - c;
    if (discriminant < 0.0f) {
        return false;
    }
    t = -b - std::sqrt(discriminant);
    if (t < 0.0f) {
        t = 0.0f;  // 始点が球の内側にある場合
    }
    return true;
}
}  // namespace

void DebugCamera::Update(
    const std::vector<PickTarget>& targets,
    float screenWidth,
    float screenHeight,
    const Matrix4x4& projection,
    bool blockMouse) {

    Input* input = Input::GetInstance();

    // Enterキーで有効・無効をトグル
    if (input->IsTrigger(DIK_RETURN)) {
        enabled_ = !enabled_;
    }

    // 無効時、またはImGui操作中はマウス入力を処理しない
    if (!enabled_ || blockMouse) {
        return;
    }

    // --- 左クリックでピッキングし、当たったオブジェクトの中心を注視点にする ---
    if (input->IsMouseTrigger(kMouseLeft)) {
        // スクリーン座標 → ワールド空間のレイを作る（逆ビュー射影でアンプロジェクト）
        Matrix4x4 viewProjection = Multiply(GetViewMatrix(), projection);
        Matrix4x4 invViewProjection = Inverse(viewProjection);

        Vector2 mouse = input->GetMousePosition();
        float ndcX = (2.0f * mouse.x / screenWidth) - 1.0f;
        float ndcY = 1.0f - (2.0f * mouse.y / screenHeight);

        Vector3 nearPoint = Transform({ ndcX, ndcY, 0.0f }, invViewProjection);  // 近クリップ面上
        Vector3 farPoint = Transform({ ndcX, ndcY, 1.0f }, invViewProjection);   // 遠クリップ面上
        Vector3 rayOrigin = nearPoint;
        Vector3 rayDirection = Normalize(farPoint - nearPoint);

        // 最も手前で当たったオブジェクトを選択する
        float nearestT = FLT_MAX;
        bool hit = false;
        Vector3 hitCenter = {};
        for (const PickTarget& target : targets) {
            float t = 0.0f;
            if (IntersectRaySphere(rayOrigin, rayDirection, target.center, target.radius, t) && t < nearestT) {
                nearestT = t;
                hitCenter = target.center;
                hit = true;
            }
        }
        if (hit) {
            target_ = hitCenter;
        }
    }

    // --- 左/右どちらかのドラッグで注視点を中心にピボット回転（ターンテーブル）---
    // 右ドラッグはピッキングしないため、カーソルが他のオブジェクトに重なっても選択は変わらない
    if (input->IsMousePress(kMouseLeft) || input->IsMousePress(kMouseRight)) {
        Vector2 move = input->GetMouseMove();
        // ドラッグ方向にオブジェクトを掴んで回す感覚（向きを逆にしたい場合は符号を反転）
        yaw_ += move.x * rotateSpeed_;
        pitch_ += move.y * rotateSpeed_;

        // 真上・真下で反転しないようにピッチを制限（約±89度）
        const float pitchLimit = 1.55f;
        if (pitch_ > pitchLimit) {
            pitch_ = pitchLimit;
        }
        if (pitch_ < -pitchLimit) {
            pitch_ = -pitchLimit;
        }
    }

    // --- ホイールでズーム（手前に回すと引き / 奥に回すと寄り）---
    float wheel = input->GetWheel();
    if (wheel != 0.0f) {
        distance_ -= wheel * zoomSpeed_;
        if (distance_ < minDistance_) {
            distance_ = minDistance_;
        }
        if (distance_ > maxDistance_) {
            distance_ = maxDistance_;
        }
    }
}

Matrix4x4 DebugCamera::MakeCameraWorldMatrix() const {
    // 回転（ロールなし）から前方向を求め、注視点からdistance_だけ後退した位置へカメラを置く
    Vector3 rotate = { pitch_, yaw_, 0.0f };
    Matrix4x4 rotateMatrix = MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, rotate, { 0.0f, 0.0f, 0.0f });
    Vector3 forward = Transform({ 0.0f, 0.0f, 1.0f }, rotateMatrix);  // カメラの前方向（+Z）
    Vector3 position = target_ - Multiply(forward, distance_);
    return MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, rotate, position);
}

Matrix4x4 DebugCamera::GetViewMatrix() const {
    return Inverse(MakeCameraWorldMatrix());
}

#ifdef USE_IMGUI
void DebugCamera::DrawImGui() {
    ImGui::Begin("Debug Camera");

    ImGui::Text("Status: %s", enabled_ ? "ON" : "OFF");
    ImGui::TextWrapped("Enter: toggle  /  Left click: select & orbit  /  Right drag: orbit only  /  Wheel: zoom");
    ImGui::Separator();

    ImGui::DragFloat3("Target", &target_.x, 0.01f);
    ImGui::DragFloat("Distance", &distance_, 0.1f, minDistance_, maxDistance_);
    ImGui::SliderAngle("Yaw", &yaw_);
    ImGui::SliderAngle("Pitch", &pitch_, -89.0f, 89.0f);
    ImGui::Separator();

    ImGui::DragFloat("Rotate Speed", &rotateSpeed_, 0.0005f, 0.0001f, 0.05f);
    ImGui::DragFloat("Zoom Speed", &zoomSpeed_, 0.001f, 0.0001f, 0.1f);

    ImGui::End();
}
#endif
