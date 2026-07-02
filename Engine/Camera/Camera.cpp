#include "Engine/Camera/Camera.h"

Matrix4x4 Camera::GetViewMatrix() const {
	Matrix4x4 cameraWorld = MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
	return Inverse(cameraWorld);
}

Matrix4x4 Camera::GetProjectionMatrix(float aspectRatio) const {
	return MakePerspectiveFovMatrix(fovY_, aspectRatio, nearClip_, farClip_);
}
