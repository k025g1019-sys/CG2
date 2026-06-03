#include "Camera.h"

Matrix4x4 Camera::GetViewMatrix() const {
    Matrix4x4 world = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
    return Inverse(world);
}