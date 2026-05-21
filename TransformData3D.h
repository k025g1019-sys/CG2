#pragma once
#include "Vector3.h"
// s, r, t
struct Transform3D {
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
};