#include "Engine/Rendering/Object3D.h"

#include <cassert>
#include <cmath>

#include "Engine/Core/DirectXCore.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Rendering/Mesh.h"

void Object3D::Initialize(ID3D12Device* device, Mesh* mesh, uint32_t textureHandle, bool enableLighting) {
	assert(mesh != nullptr);

	mesh_ = mesh;
	textureHandle_ = textureHandle;

	material_.color = { 1.0f, 1.0f, 1.0f, 1.0f };
	material_.enableLighting = enableLighting;
	material_.uvTransform = MakeIdentity4x4();

	transformCB_.Create(device, DirectXCore::kFramesInFlight);
	materialCB_.Create(device, DirectXCore::kFramesInFlight);
}

void Object3D::Update(const Frustum3D& frustum) {
	// 視錐台カリング判定（Outsideの場合はDrawで描画をスキップする）
	visibility_ = ClassifyFrustum(frustum, CalcWorldBoundingSphere());

	// ワールド行列を計算して定数バッファへ書き込む
	Matrix4x4 world = MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
	TransformationMatrix transformData{ world };

	uint32_t frameIndex = DirectXCore::GetInstance()->GetFrameIndex();
	transformCB_.Write(frameIndex, transformData);
	materialCB_.Write(frameIndex, material_);
}

void Object3D::Draw(ID3D12GraphicsCommandList* commandList) const {
	if (!IsVisible(visibility_)) {
		return;
	}

	uint32_t frameIndex = DirectXCore::GetInstance()->GetFrameIndex();
	commandList->SetGraphicsRootConstantBufferView(0, materialCB_.GetGPUAddress(frameIndex));
	commandList->SetGraphicsRootConstantBufferView(1, transformCB_.GetGPUAddress(frameIndex));
	commandList->SetGraphicsRootDescriptorTable(3, TextureManager::GetInstance()->GetSrvHandleGPU(textureHandle_));

	mesh_->Draw(commandList);
}

Sphere Object3D::CalcWorldBoundingSphere() const {
	Matrix4x4 world = MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
	Vector3 center = Transform(mesh_->GetLocalCenter(), world);

	// 拡大率の最大成分で半径をスケールする
	// （std::maxはWindows.hのmin/maxマクロと衝突するため比較で求める）
	float maxScale = std::fabs(transform_.scale.x);
	if (std::fabs(transform_.scale.y) > maxScale) { maxScale = std::fabs(transform_.scale.y); }
	if (std::fabs(transform_.scale.z) > maxScale) { maxScale = std::fabs(transform_.scale.z); }

	return Sphere{ center, mesh_->GetLocalRadius() * maxScale };
}
