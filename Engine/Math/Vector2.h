#pragma once
struct Vector2 {
	float x, y;

	Vector2 operator+(const Vector2& v) const { return { x + v.x, y + v.y }; }
	Vector2 operator-(const Vector2& v) const { return { x - v.x, y - v.y }; }
	Vector2 operator-() const { return { -x, -y }; }
	Vector2 operator*(float s) const { return { x * s, y * s }; }
	Vector2 operator/(float s) const { return { x / s, y / s }; }

	Vector2& operator+=(const Vector2& v) { x += v.x; y += v.y; return *this; }
	Vector2& operator-=(const Vector2& v) { x -= v.x; y -= v.y; return *this; }
	Vector2& operator*=(float s) { x *= s; y *= s; return *this; }
};

inline Vector2 operator*(float s, const Vector2& v) { return { v.x * s, v.y * s }; }
