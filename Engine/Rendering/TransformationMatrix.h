#pragma once
#include "Matrix4x4.h"

// オブジェクトごとの定数バッファ（ワールド行列のみ。視点に依存しない）。
// ビュー射影は視点ごとの別CBuffer（ViewProjectionResource）で供給する。
struct TransformationMatrix {
    Matrix4x4 World;
};
