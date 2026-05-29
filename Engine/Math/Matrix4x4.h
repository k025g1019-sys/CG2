#pragma once
#include "Vector3.h"

struct Matrix4x4 {
	float m[4][4];
};

#pragma region

// 行列の加法
Matrix4x4 Add(const Matrix4x4& m1, const Matrix4x4& m2);
// 行列の減法
Matrix4x4 Subtract(const Matrix4x4& m1, const Matrix4x4& m2);
// 行列の積
Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2);
// 逆行列
Matrix4x4 Inverse(const Matrix4x4& m);
// 転置行列
Matrix4x4 Transpose(const Matrix4x4& m);
// 単位行列の作成
Matrix4x4 MakeIdentity4x4();

#pragma endregion

#pragma region

// 平行移動行列
Matrix4x4 MakeTranslateMatrix(const Vector3& translate);
// 拡大縮小行列
Matrix4x4 MakeScaleMatrix(const Vector3& scale);
// 座標変換
Vector3 Transform(const Vector3& vector, const Matrix4x4& matrix);

// X軸回転行列
Matrix4x4 MakeRotateXMatrix(float radian);
// Y軸回転行列
Matrix4x4 MakeRotateYMatrix(float radian);
// Z軸回転行列
Matrix4x4 MakeRotateZMatrix(float radian);

// 3次元アフィン変換行列
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate);

#pragma endregion

#pragma region

// 透視投影行列
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);
// 正射影行列
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);
// ビューポート行列
Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth);

// クロス積
Vector3 Cross(const Vector3& v1, const Vector3& v2);

#pragma endregion

#pragma region

// 内積
float Dot(const Vector3& v1, const Vector3& v2);

// ベクトル加算
Vector3 Add(const Vector3& v1, const Vector3& v2);

// ベクトル減算
Vector3 Subtract(const Vector3& v1, const Vector3& v2);

// ベクトルスカラー倍
Vector3 Multiply(const Vector3& v, float s);

// 正射影ベクトル
Vector3 Project(const Vector3& v1, const Vector3& v2);

#pragma endregion

#pragma region

Vector3 Multiply(float s, const Vector3& v);

// ベクトルの長さ
float Length(const Vector3& v);

// 正規化
Vector3 Normalize(const Vector3& v);

#pragma endregion