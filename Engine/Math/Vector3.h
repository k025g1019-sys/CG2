#pragma once
struct Vector3 {
	float x, y, z;

	Vector3 operator+(const Vector3& v) const { return { x + v.x, y + v.y, z + v.z }; }
	Vector3 operator-(const Vector3& v) const { return { x - v.x, y - v.y, z - v.z }; }
	Vector3 operator-() const { return { -x, -y, -z }; }
	Vector3 operator*(float s) const { return { x * s, y * s, z * s }; }
	Vector3 operator/(float s) const { return { x / s, y / s, z / s }; }

	Vector3& operator+=(const Vector3& v) { x += v.x; y += v.y; z += v.z; return *this; }
	Vector3& operator-=(const Vector3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
	Vector3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};

inline Vector3 operator*(float s, const Vector3& v) { return { v.x * s, v.y * s, v.z * s }; }
