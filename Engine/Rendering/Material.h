#pragma once
#include <cstdint>
#include "Engine/Math/Vector4.h"
#include "Engine/Math/Matrix4x4.h"

struct Material {
    Vector4 color;
    int32_t enableLighting;
    float padding[3];
    Matrix4x4 uvTransform;
};