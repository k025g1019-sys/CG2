#pragma once

#include "Engine/Math/Vector4.h"
#include "Engine/Math/Vector3.h"

struct DirectionalLight {
    Vector4 color;
    Vector3 direction;
    float intensity;
};