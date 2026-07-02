#include "Engine/Math/Matrix4x4.h"
#include <algorithm>
#include <assert.h>
#include <cmath>

#pragma region

// 行列の加法
Matrix4x4 Add(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result{};
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = m1.m[i][j] + m2.m[i][j];
		}
	}
	return result;
};

// 行列の減法
Matrix4x4 Subtract(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result{};
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = m1.m[i][j] - m2.m[i][j];
		}
	}
	return result;
};

// 行列の積
Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result{};
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = m1.m[i][0] * m2.m[0][j] + m1.m[i][1] * m2.m[1][j] + m1.m[i][2] * m2.m[2][j] + m1.m[i][3] * m2.m[3][j];
		}
	}
	return result;
};

Matrix4x4 Matrix4x4::operator+(const Matrix4x4& rhs) const {
	return Add(*this, rhs);
}

Matrix4x4 Matrix4x4::operator-(const Matrix4x4& rhs) const {
	return Subtract(*this, rhs);
}

Matrix4x4 Matrix4x4::operator*(const Matrix4x4& rhs) const {
	return Multiply(*this, rhs);
}

Matrix4x4& Matrix4x4::operator*=(const Matrix4x4& rhs) {
	*this = Multiply(*this, rhs);
	return *this;
}

// 逆行列
Matrix4x4 Inverse(const Matrix4x4& m) {
	Matrix4x4 a = m;                   // 作業用
	Matrix4x4 inv = MakeIdentity4x4(); // 単位行列

	for (int i = 0; i < 4; i++) {
		// ピボット選択（0 なら失敗）
		float pivot = a.m[i][i];
		if (fabs(pivot) < 1e-6f) {
			// ピボットが小さすぎる場合、行を交換する
			for (int r = i + 1; r < 4; r++) {
				if (fabs(a.m[r][i]) > 1e-6f) {
					// std::swap ... 変数同士の値を入れ替える
					std::swap(a.m[i], a.m[r]);
					std::swap(inv.m[i], inv.m[r]);
					pivot = a.m[i][i];
					break;
				}
			}
		}

		// それでも pivot が 0 なら逆行列なし
		if (fabs(pivot) < 1e-6f) {
			return MakeIdentity4x4(); // 失敗時の代替（適宜変更）
		}

		// ピボット行を 1 に正規化
		float invPivot = 1.0f / pivot;
		for (int j = 0; j < 4; j++) {
			a.m[i][j] *= invPivot;
			inv.m[i][j] *= invPivot;
		}

		// 他の行からピボット列を消去
		for (int r = 0; r < 4; r++) {
			if (r == i)
				continue;
			float factor = a.m[r][i];
			for (int c = 0; c < 4; c++) {
				a.m[r][c] -= factor * a.m[i][c];
				inv.m[r][c] -= factor * inv.m[i][c];
			}
		}
	}

	return inv;
}

// 転置行列
Matrix4x4 Transpose(const Matrix4x4& m) {
	Matrix4x4 result{};
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = m.m[j][i];
		}
	}
	return result;
};

// 単位行列の作成
Matrix4x4 MakeIdentity4x4() {
	Matrix4x4 result{};
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = (i == j) ? 1.0f : 0.0f;
		}
	}
	return result;
}

#pragma endregion

#pragma region

// 平行移動行列
Matrix4x4 MakeTranslateMatrix(const Vector3& translate) {
	Matrix4x4 result = MakeIdentity4x4();
	result.m[3][0] = translate.x;
	result.m[3][1] = translate.y;
	result.m[3][2] = translate.z;
	return result;
}
// 拡大縮小行列
Matrix4x4 MakeScaleMatrix(const Vector3& scale) {
	Matrix4x4 result = MakeIdentity4x4();
	result.m[0][0] = scale.x;
	result.m[1][1] = scale.y;
	result.m[2][2] = scale.z;
	return result;
}
// 座標変換
Vector3 Transform(const Vector3& vector, const Matrix4x4& matrix) {
	Vector3 result{};
	result.x = vector.x * matrix.m[0][0] + vector.y * matrix.m[1][0] + vector.z * matrix.m[2][0] + matrix.m[3][0];
	result.y = vector.x * matrix.m[0][1] + vector.y * matrix.m[1][1] + vector.z * matrix.m[2][1] + matrix.m[3][1];
	result.z = vector.x * matrix.m[0][2] + vector.y * matrix.m[1][2] + vector.z * matrix.m[2][2] + matrix.m[3][2];
	float w = vector.x * matrix.m[0][3] + vector.y * matrix.m[1][3] + vector.z * matrix.m[2][3] + matrix.m[3][3];
	assert(w != 0.0f);
	result.x /= w;
	result.y /= w;
	result.z /= w;
	return result;
}

// X軸回転行列
Matrix4x4 MakeRotateXMatrix(float radian) {
	Matrix4x4 result = MakeIdentity4x4();
	float c = std::cos(radian);
	float s = std::sin(radian);

	result.m[1][1] = c;
	result.m[1][2] = s;
	result.m[2][1] = -s;
	result.m[2][2] = c;

	return result;
}

// Y軸回転行列
Matrix4x4 MakeRotateYMatrix(float radian) {
	Matrix4x4 result = MakeIdentity4x4();
	float c = std::cos(radian);
	float s = std::sin(radian);

	result.m[0][0] = c;
	result.m[0][2] = -s;
	result.m[2][0] = s;
	result.m[2][2] = c;

	return result;
}

// Z軸回転行列
Matrix4x4 MakeRotateZMatrix(float radian) {
	Matrix4x4 result = MakeIdentity4x4();
	float c = std::cos(radian);
	float s = std::sin(radian);

	result.m[0][0] = c;
	result.m[0][1] = s;
	result.m[1][0] = -s;
	result.m[1][1] = c;

	return result;
}

// 3次元アフィン変換行列
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
	// 各行列を作成
	Matrix4x4 scaleMat = MakeScaleMatrix(scale);
	Matrix4x4 rotXMat = MakeRotateXMatrix(rotate.x);
	Matrix4x4 rotYMat = MakeRotateYMatrix(rotate.y);
	Matrix4x4 rotZMat = MakeRotateZMatrix(rotate.z);
	Matrix4x4 transMat = MakeTranslateMatrix(translate);

	// 回転行列を合成（X → Y → Z）
	Matrix4x4 rotMat = Multiply(rotXMat, Multiply(rotYMat, rotZMat));

	// アフィン行列 = S * R * T
	Matrix4x4 affine = Multiply(scaleMat, Multiply(rotMat, transMat));

	return affine;
}

#pragma endregion

#pragma region

// 透視投影行列
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip) {
	Matrix4x4 result{};

	float yScale = 1.0f / std::tan(fovY * 0.5f);
	float xScale = yScale / aspectRatio;

	result.m[0][0] = xScale;
	result.m[1][1] = yScale;
	result.m[2][2] = farClip / (farClip - nearClip);
	result.m[2][3] = 1.0f;
	result.m[3][2] = (-nearClip * farClip) / (farClip - nearClip);

	return result;
}

// 正射影行列
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip) {
	Matrix4x4 result{};

	result.m[0][0] = 2.0f / (right - left);
	result.m[1][1] = 2.0f / (top - bottom);
	result.m[2][2] = 1.0f / (farClip - nearClip);

	result.m[3][0] = (left + right) / (left - right);
	result.m[3][1] = (top + bottom) / (bottom - top);
	result.m[3][2] = nearClip / (nearClip - farClip);
	result.m[3][3] = 1.0f;

	return result;
}

// ビューポート行列
Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth) {
	Matrix4x4 result = MakeIdentity4x4();

	result.m[0][0] = width / 2.0f;
	result.m[1][1] = -height / 2.0f;
	result.m[2][2] = maxDepth - minDepth;

	result.m[3][0] = left + width / 2.0f;
	result.m[3][1] = top + height / 2.0f;
	result.m[3][2] = minDepth;

	return result;
}

// クロス積
Vector3 Cross(const Vector3& v1, const Vector3& v2) {
	Vector3 result{};
	result.x = v1.y * v2.z - v1.z * v2.y;
	result.y = v1.z * v2.x - v1.x * v2.z;
	result.z = v1.x * v2.y - v1.y * v2.x;
	return result;
}

#pragma endregion

#pragma region

// 内積
float Dot(const Vector3& v1, const Vector3& v2) { return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z; }

// ベクトル加算
Vector3 Add(const Vector3& v1, const Vector3& v2) { return { v1.x + v2.x, v1.y + v2.y, v1.z + v2.z }; }

// ベクトル減算
Vector3 Subtract(const Vector3& v1, const Vector3& v2) { return { v1.x - v2.x, v1.y - v2.y, v1.z - v2.z }; }

// ベクトルスカラー倍
Vector3 Multiply(const Vector3& v, float s) { return { v.x * s, v.y * s, v.z * s }; }

// 正射影ベクトル
Vector3 Project(const Vector3& v1, const Vector3& v2) {
	float dotVV = Dot(v2, v2);
	if (dotVV < 1e-6f) {
		return { 0.0f, 0.0f, 0.0f }; // v2 がゼロベクトルの場合
	}
	float t = Dot(v1, v2) / dotVV;
	return Multiply(v2, t);
}

#pragma endregion

#pragma region

Vector3 Multiply(float s, const Vector3& v) {
	return { v.x * s, v.y * s, v.z * s };
}

// ベクトルの長さ
float Length(const Vector3& v) {
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// 正規化
Vector3 Normalize(const Vector3& v) {
	float len = Length(v);
	constexpr float EPSILON = 1e-6f;

	// 0除算防止
	if (len < EPSILON) {
		return { 0.0f, 0.0f, 0.0f };
	}

	return { v.x / len, v.y / len, v.z / len };
}

#pragma endregion