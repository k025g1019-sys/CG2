#pragma once
#include "Engine/Math/Matrix4x4.h"

// オブジェクトごとのワールド行列（VSのb0と一致させる）。
// ビュー射影は視点ごとの別CBuffer（VSのb1）で供給する（立体視の視点切り替え用）。
struct TransformationMatrix {
    Matrix4x4 World;
};
