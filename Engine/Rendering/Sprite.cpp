#include "Engine/Rendering/Sprite.h"

#include "Engine/Core/DirectXCore.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Math/Matrix4x4.h"

void Sprite::Initialize(ID3D12Device* device, uint32_t textureHandle, const Vector2& size) {
	textureHandle_ = textureHandle;

	// クアッド（左上原点のスクリーン座標系。0:左下 / 1:左上 / 2:右下 / 3:右上）
	VertexData vertices[4]{};
	vertices[0].position = { 0.0f, size.y, 0.0f, 1.0f };
	vertices[1].position = { 0.0f, 0.0f, 0.0f, 1.0f };
	vertices[2].position = { size.x, size.y, 0.0f, 1.0f };
	vertices[3].position = { size.x, 0.0f, 0.0f, 1.0f };
	vertices[0].texcoord = { 0.0f, 1.0f };
	vertices[1].texcoord = { 0.0f, 0.0f };
	vertices[2].texcoord = { 1.0f, 1.0f };
	vertices[3].texcoord = { 1.0f, 0.0f };
	for (VertexData& vertex : vertices) {
		vertex.normal = { 0.0f, 0.0f, -1.0f };
	}

	const uint32_t indices[6] = { 0, 1, 2, 1, 3, 2 };

	mesh_.Create(device, vertices, 4, indices, 6);

	// 2Dなのでライティング無効
	material_.color = { 1.0f, 1.0f, 1.0f, 1.0f };
	material_.enableLighting = false;
	material_.uvTransform = MakeIdentity4x4();

	transformCB_.Create(device, DirectXCore::kFramesInFlight);
	materialCB_.Create(device, DirectXCore::kFramesInFlight);
	viewProjectionCB_.Create(device, DirectXCore::kFramesInFlight);
}

void Sprite::Update(float screenWidth, float screenHeight) {
	// ワールド行列と正射影（画面左上原点）を計算する
	Matrix4x4 world = MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
	Matrix4x4 projection = MakeOrthographicMatrix(0.0f, 0.0f, screenWidth, screenHeight, 0.0f, 100.0f);
	TransformationMatrix transformData{ world };

	// UV変換行列
	Matrix4x4 uvTransformMatrix = MakeScaleMatrix(uvTransform_.scale);
	uvTransformMatrix *= MakeRotateZMatrix(uvTransform_.rotate.z);
	uvTransformMatrix *= MakeTranslateMatrix(uvTransform_.translate);
	material_.uvTransform = uvTransformMatrix;

	uint32_t frameIndex = DirectXCore::GetInstance()->GetFrameIndex();
	transformCB_.Write(frameIndex, transformData);
	materialCB_.Write(frameIndex, material_);
	viewProjectionCB_.Write(frameIndex, projection);

	// --- 2D視錐台カリング（可視範囲は画面矩形）---
	// 4頂点をワールド変換してスクリーン空間のAABBを作り、画面矩形と判定する
	const VertexData* vertices = mesh_.GetMappedVertices();
	Vector2 spriteMin{ 0.0f, 0.0f };
	Vector2 spriteMax{ 0.0f, 0.0f };
	for (int i = 0; i < 4; ++i) {
		Vector3 local{ vertices[i].position.x, vertices[i].position.y, vertices[i].position.z };
		Vector3 screen = Transform(local, world);
		if (i == 0) {
			spriteMin = { screen.x, screen.y };
			spriteMax = { screen.x, screen.y };
		} else {
			if (screen.x < spriteMin.x) { spriteMin.x = screen.x; }
			if (screen.y < spriteMin.y) { spriteMin.y = screen.y; }
			if (screen.x > spriteMax.x) { spriteMax.x = screen.x; }
			if (screen.y > spriteMax.y) { spriteMax.y = screen.y; }
		}
	}
	Frustum2D screenFrustum = MakeFrustumFromRect({ 0.0f, 0.0f }, { screenWidth, screenHeight });
	visibility_ = ClassifyFrustum(screenFrustum, AABB2D{ spriteMin, spriteMax });
}

void Sprite::Draw(ID3D12GraphicsCommandList* commandList) const {
	if (!IsVisible(visibility_)) {
		return;
	}

	uint32_t frameIndex = DirectXCore::GetInstance()->GetFrameIndex();
	commandList->SetGraphicsRootConstantBufferView(0, materialCB_.GetGPUAddress(frameIndex));
	commandList->SetGraphicsRootConstantBufferView(1, transformCB_.GetGPUAddress(frameIndex));
	commandList->SetGraphicsRootDescriptorTable(3, TextureManager::GetInstance()->GetSrvHandleGPU(textureHandle_));
	// 2Dなのでビュー射影を自前の正射影へ差し替える（3D側は次の視点描画で再バインドされる）
	commandList->SetGraphicsRootConstantBufferView(4, viewProjectionCB_.GetGPUAddress(frameIndex));

	mesh_.Draw(commandList);
}
