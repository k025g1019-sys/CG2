#include "FrustumCulling.h"

#include <cmath>

namespace {

// 法線(a,b,c)と距離dの平面を、法線を単位長に正規化して返す。
Plane3D NormalizePlane3D(float a, float b, float c, float d) {
	float length = std::sqrt(a * a + b * b + c * c);
	if (length < 1e-6f) {
		// 退化（通常の視錐台では起きない）。ゼロ法線のまま返す。
		return Plane3D{ { 0.0f, 0.0f, 0.0f }, d };
	}
	float inv = 1.0f / length;
	return Plane3D{ { a * inv, b * inv, c * inv }, d * inv };
}

// 平面に対する点の符号付き距離（正なら内側）。
float SignedDistance(const Plane3D& plane, const Vector3& point) {
	return Dot(plane.normal, point) + plane.distance;
}
float SignedDistance(const Plane2D& plane, const Vector2& point) {
	return plane.normal.x * point.x + plane.normal.y * point.y + plane.distance;
}

// 頂点集合と平面集合から3状態を求める（箱・線分・三角形で共用）。
//   ・ある1平面の外側に全頂点 → Outside
//   ・全平面で全頂点が内側     → Inside
//   ・それ以外                 → Intersect
FrustumVisibility ClassifyPoints(const Plane3D* planes, int planeCount, const Vector3* points, int pointCount) {
	bool intersect = false;
	for (int i = 0; i < planeCount; ++i) {
		int outCount = 0;
		for (int k = 0; k < pointCount; ++k) {
			if (SignedDistance(planes[i], points[k]) < 0.0f) {
				++outCount;
			}
		}
		if (outCount == pointCount) {
			return FrustumVisibility::Outside;  // この平面の外側に全頂点がある
		}
		if (outCount > 0) {
			intersect = true;  // この平面をまたいでいる
		}
	}
	return intersect ? FrustumVisibility::Intersect : FrustumVisibility::Inside;
}
FrustumVisibility ClassifyPoints(const Plane2D* planes, int planeCount, const Vector2* points, int pointCount) {
	bool intersect = false;
	for (int i = 0; i < planeCount; ++i) {
		int outCount = 0;
		for (int k = 0; k < pointCount; ++k) {
			if (SignedDistance(planes[i], points[k]) < 0.0f) {
				++outCount;
			}
		}
		if (outCount == pointCount) {
			return FrustumVisibility::Outside;
		}
		if (outCount > 0) {
			intersect = true;
		}
	}
	return intersect ? FrustumVisibility::Intersect : FrustumVisibility::Inside;
}

}  // namespace

// ===================== 視錐台の生成 =====================

Frustum3D MakeFrustumFromViewProjection(const Matrix4x4& m) {
	// 行ベクトル規約（clip = p * M）でのGribb–Hartmann法。
	// 各平面の係数 = 「列j ± 列3」、成分iは m[i][j]。内向き法線が得られる。
	Frustum3D frustum;
	// Left  : clip.x + clip.w >= 0  →  列0 + 列3
	frustum.planes[0] = NormalizePlane3D(
		m.m[0][0] + m.m[0][3], m.m[1][0] + m.m[1][3], m.m[2][0] + m.m[2][3], m.m[3][0] + m.m[3][3]);
	// Right : clip.w - clip.x >= 0  →  列3 - 列0
	frustum.planes[1] = NormalizePlane3D(
		m.m[0][3] - m.m[0][0], m.m[1][3] - m.m[1][0], m.m[2][3] - m.m[2][0], m.m[3][3] - m.m[3][0]);
	// Bottom: clip.y + clip.w >= 0  →  列1 + 列3
	frustum.planes[2] = NormalizePlane3D(
		m.m[0][1] + m.m[0][3], m.m[1][1] + m.m[1][3], m.m[2][1] + m.m[2][3], m.m[3][1] + m.m[3][3]);
	// Top   : clip.w - clip.y >= 0  →  列3 - 列1
	frustum.planes[3] = NormalizePlane3D(
		m.m[0][3] - m.m[0][1], m.m[1][3] - m.m[1][1], m.m[2][3] - m.m[2][1], m.m[3][3] - m.m[3][1]);
	// Near  : clip.z >= 0（DirectXは深度0..1）  →  列2
	frustum.planes[4] = NormalizePlane3D(
		m.m[0][2], m.m[1][2], m.m[2][2], m.m[3][2]);
	// Far   : clip.w - clip.z >= 0  →  列3 - 列2
	frustum.planes[5] = NormalizePlane3D(
		m.m[0][3] - m.m[0][2], m.m[1][3] - m.m[1][2], m.m[2][3] - m.m[2][2], m.m[3][3] - m.m[3][2]);
	return frustum;
}

Frustum2D MakeFrustumFromRect(const Vector2& min, const Vector2& max) {
	Frustum2D frustum;
	// Left  : x >= min.x  →  ( 1,  0), d = -min.x
	frustum.planes[0] = Plane2D{ { 1.0f, 0.0f }, -min.x };
	// Right : x <= max.x  →  (-1,  0), d =  max.x
	frustum.planes[1] = Plane2D{ { -1.0f, 0.0f }, max.x };
	// Bottom: y >= min.y  →  ( 0,  1), d = -min.y
	frustum.planes[2] = Plane2D{ { 0.0f, 1.0f }, -min.y };
	// Top   : y <= max.y  →  ( 0, -1), d =  max.y
	frustum.planes[3] = Plane2D{ { 0.0f, -1.0f }, max.y };
	return frustum;
}

// ===================== 判定（3D）=====================

FrustumVisibility ClassifyFrustum(const Frustum3D& frustum, const Sphere& sphere) {
	bool intersect = false;
	for (int i = 0; i < 6; ++i) {
		float dist = SignedDistance(frustum.planes[i], sphere.center);
		if (dist < -sphere.radius) {
			return FrustumVisibility::Outside;  // 中心が半径より深く外側
		}
		if (dist < sphere.radius) {
			intersect = true;  // この平面に跨る
		}
	}
	return intersect ? FrustumVisibility::Intersect : FrustumVisibility::Inside;
}

FrustumVisibility ClassifyFrustum(const Frustum3D& frustum, const AABB3D& aabb) {
	const Vector3 points[8] = {
		{ aabb.min.x, aabb.min.y, aabb.min.z },
		{ aabb.max.x, aabb.min.y, aabb.min.z },
		{ aabb.min.x, aabb.max.y, aabb.min.z },
		{ aabb.max.x, aabb.max.y, aabb.min.z },
		{ aabb.min.x, aabb.min.y, aabb.max.z },
		{ aabb.max.x, aabb.min.y, aabb.max.z },
		{ aabb.min.x, aabb.max.y, aabb.max.z },
		{ aabb.max.x, aabb.max.y, aabb.max.z },
	};
	return ClassifyPoints(frustum.planes, 6, points, 8);
}

FrustumVisibility ClassifyFrustum(const Frustum3D& frustum, const OBB3D& obb) {
	// 各軸方向の半幅ベクトル
	Vector3 ex = Multiply(obb.orientations[0], obb.size.x);
	Vector3 ey = Multiply(obb.orientations[1], obb.size.y);
	Vector3 ez = Multiply(obb.orientations[2], obb.size.z);
	Vector3 points[8];
	int index = 0;
	for (int sx = -1; sx <= 1; sx += 2) {
		for (int sy = -1; sy <= 1; sy += 2) {
			for (int sz = -1; sz <= 1; sz += 2) {
				points[index++] = obb.center
					+ Multiply(ex, float(sx))
					+ Multiply(ey, float(sy))
					+ Multiply(ez, float(sz));
			}
		}
	}
	return ClassifyPoints(frustum.planes, 6, points, 8);
}

FrustumVisibility ClassifyFrustum(const Frustum3D& frustum, const Segment3D& segment) {
	const Vector3 points[2] = { segment.start, segment.end };
	return ClassifyPoints(frustum.planes, 6, points, 2);
}

FrustumVisibility ClassifyFrustum(const Frustum3D& frustum, const Triangle3D& triangle) {
	const Vector3 points[3] = { triangle.v0, triangle.v1, triangle.v2 };
	return ClassifyPoints(frustum.planes, 6, points, 3);
}

// ===================== 判定（2D）=====================

FrustumVisibility ClassifyFrustum(const Frustum2D& frustum, const Circle& circle) {
	bool intersect = false;
	for (int i = 0; i < 4; ++i) {
		float dist = SignedDistance(frustum.planes[i], circle.center);
		if (dist < -circle.radius) {
			return FrustumVisibility::Outside;
		}
		if (dist < circle.radius) {
			intersect = true;
		}
	}
	return intersect ? FrustumVisibility::Intersect : FrustumVisibility::Inside;
}

FrustumVisibility ClassifyFrustum(const Frustum2D& frustum, const AABB2D& aabb) {
	const Vector2 points[4] = {
		{ aabb.min.x, aabb.min.y },
		{ aabb.max.x, aabb.min.y },
		{ aabb.min.x, aabb.max.y },
		{ aabb.max.x, aabb.max.y },
	};
	return ClassifyPoints(frustum.planes, 4, points, 4);
}

FrustumVisibility ClassifyFrustum(const Frustum2D& frustum, const OBB2D& obb) {
	// 各軸方向の半幅ベクトル
	Vector2 ex = { obb.orientations[0].x * obb.size.x, obb.orientations[0].y * obb.size.x };
	Vector2 ey = { obb.orientations[1].x * obb.size.y, obb.orientations[1].y * obb.size.y };
	const Vector2 points[4] = {
		{ obb.center.x - ex.x - ey.x, obb.center.y - ex.y - ey.y },
		{ obb.center.x + ex.x - ey.x, obb.center.y + ex.y - ey.y },
		{ obb.center.x - ex.x + ey.x, obb.center.y - ex.y + ey.y },
		{ obb.center.x + ex.x + ey.x, obb.center.y + ex.y + ey.y },
	};
	return ClassifyPoints(frustum.planes, 4, points, 4);
}

FrustumVisibility ClassifyFrustum(const Frustum2D& frustum, const Segment2D& segment) {
	const Vector2 points[2] = { segment.start, segment.end };
	return ClassifyPoints(frustum.planes, 4, points, 2);
}

FrustumVisibility ClassifyFrustum(const Frustum2D& frustum, const Triangle2D& triangle) {
	const Vector2 points[3] = { triangle.v0, triangle.v1, triangle.v2 };
	return ClassifyPoints(frustum.planes, 4, points, 3);
}
