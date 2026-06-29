#include "RenderResource.h"

#include "GpuResource.h"
#include "Matrix4x4.h"
#include "TransformData3D.h"
#include "TransformationMatrix.h"
#include "Material.h"
#include "Engine/Light/DirectionalLight.h"

#pragma region TransformationMatrix（World）

TransformResource CreateTransformResource(ID3D12Device* device) {
	TransformResource result;

	result.resource = CreateBufferResource(device, sizeof(TransformationMatrix));
	result.resource->Map(0, nullptr, reinterpret_cast<void**>(&result.data));

	result.data->World = MakeIdentity4x4();

	return result;
}

void UpdateTransformMatrix(
	TransformResource& transformResource,
	const Transform3D& transform) {
	Matrix4x4 world = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	transformResource.data->World = world;
}

#pragma endregion

#pragma region ViewProjection（視点ごとのビュー射影行列）

ViewProjectionResource CreateViewProjectionResource(ID3D12Device* device) {
	ViewProjectionResource result;

	result.resource = CreateBufferResource(device, sizeof(Matrix4x4));
	result.resource->Map(0, nullptr, reinterpret_cast<void**>(&result.data));

	*result.data = MakeIdentity4x4();

	return result;
}

void UpdateViewProjection(
	ViewProjectionResource& viewProjectionResource,
	const Matrix4x4& viewProjection) {
	*viewProjectionResource.data = viewProjection;
}

#pragma endregion

#pragma region Material

MaterialResource CreateMaterialResource(ID3D12Device* device, bool enableLighting) {
	MaterialResource result;

	result.resource = CreateBufferResource(device, sizeof(Material));
	result.resource->Map(0, nullptr, reinterpret_cast<void**>(&result.data));

	result.data->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	result.data->enableLighting = enableLighting;
	result.data->uvTransform = MakeIdentity4x4();

	return result;
}

#pragma endregion

#pragma region DirectionalLight

DirectionalLightResource CreateDirectionalLight(ID3D12Device* device) {
	DirectionalLightResource result;

	result.resource = CreateBufferResource(device, sizeof(DirectionalLight));
	result.resource->Map(0, nullptr, reinterpret_cast<void**>(&result.data));

	result.data->color = { 1, 1, 1, 1 };
	result.data->direction = { 0, -1, 0 };
	result.data->intensity = 1.0f;

	return result;
}

#pragma endregion

#pragma region 描画

void DrawObject(
	ID3D12GraphicsCommandList* commandList,
	D3D12_VERTEX_BUFFER_VIEW& vbv,
	D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
	ID3D12Resource* transformResource,
	uint32_t vertexCount) {
	commandList->SetGraphicsRootDescriptorTable(3, textureHandle);
	commandList->IASetVertexBuffers(0, 1, &vbv);
	commandList->SetGraphicsRootConstantBufferView(1, transformResource->GetGPUVirtualAddress());
	commandList->DrawInstanced(vertexCount, 1, 0, 0);
}

#pragma endregion
