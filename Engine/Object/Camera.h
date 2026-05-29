#pragma once
#include "TransformData3D.h"
#include "Matrix4x4.h"

class Camera {
public:
    Transform3D transform;

    Matrix4x4 GetViewMatrix() const;
};