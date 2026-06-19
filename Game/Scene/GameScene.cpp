#include "GameScene.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "Engine/Core/WinApp.h"
#include "Engine/Core/DirectXCore.h"
#include "Engine/Graphics/GpuResource.h"
#include "Engine/Geometry/GeometryGenerator.h"
#include "Engine/Light/DirectionalLight.h"
#include "Engine/Audio/Audio.h"
#include "Engine/Input/Input.h"
#include "Matrix4x4.h"
#include "VertexData.h"
#include "Material.h"
#include "TransformationMatrix.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

namespace {
#ifndef NDEBUG
// 頂点群からローカル空間のバウンディング球（中心と半径）をAABB経由で求める（デバッグカメラのピッキング用）
void ComputeLocalBoundingSphere(const VertexData* vertices, size_t count, Vector3& outCenter, float& outRadius) {
	if (count == 0) {
		outCenter = { 0.0f, 0.0f, 0.0f };
		outRadius = 0.0f;
		return;
	}
	Vector3 minPos = { vertices[0].position.x, vertices[0].position.y, vertices[0].position.z };
	Vector3 maxPos = minPos;
	// std::min/maxはWindows.hのmin/maxマクロと衝突するため、比較で求める
	for (size_t i = 1; i < count; ++i) {
		float px = vertices[i].position.x;
		float py = vertices[i].position.y;
		float pz = vertices[i].position.z;
		if (px < minPos.x) { minPos.x = px; }
		if (py < minPos.y) { minPos.y = py; }
		if (pz < minPos.z) { minPos.z = pz; }
		if (px > maxPos.x) { maxPos.x = px; }
		if (py > maxPos.y) { maxPos.y = py; }
		if (pz > maxPos.z) { maxPos.z = pz; }
	}
	outCenter = { (minPos.x + maxPos.x) * 0.5f, (minPos.y + maxPos.y) * 0.5f, (minPos.z + maxPos.z) * 0.5f };
	// 中心から最も遠い頂点までの距離を半径にする
	float radiusSq = 0.0f;
	for (size_t i = 0; i < count; ++i) {
		float dx = vertices[i].position.x - outCenter.x;
		float dy = vertices[i].position.y - outCenter.y;
		float dz = vertices[i].position.z - outCenter.z;
		float distSq = dx * dx + dy * dy + dz * dz;
		if (distSq > radiusSq) {
			radiusSq = distSq;
		}
	}
	outRadius = std::sqrt(radiusSq);
}
#endif  // !NDEBUG
}  // namespace

void GameScene::Initialize(
	ID3D12Device* device,
	ID3D12RootSignature* rootSignature,
	IDxcBlob* vertexShader,
	IDxcBlob* pixelShader) {
	// --- 三角形の頂点リソース ---
	vertexResourceTriangle_ = CreateBufferResource(device, sizeof(VertexData) * 6);
	vbvTriangle_.BufferLocation = vertexResourceTriangle_->GetGPUVirtualAddress();
	vbvTriangle_.SizeInBytes = sizeof(VertexData) * 6;
	vbvTriangle_.StrideInBytes = sizeof(VertexData);
	vertexResourceTriangle_->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataTriangle_));
	// 1枚目の三角形
	vertexDataTriangle_[0].position = { -0.5f, -0.5f, 0.0f, 1.0f }; // 左下
	vertexDataTriangle_[0].texcoord = { 0.0f, 1.0f };
	vertexDataTriangle_[1].position = { 0.0f, 0.5f, 0.0f, 1.0f }; // 上
	vertexDataTriangle_[1].texcoord = { 0.5f, 0.0f };
	vertexDataTriangle_[2].position = { 0.5f, -0.5f, 0.0f, 1.0f }; // 右下
	vertexDataTriangle_[2].texcoord = { 1.0f, 1.0f };
	// 1枚目を貫通する三角形
	vertexDataTriangle_[3].position = { -0.5f, -0.5f, 0.5f, 1.0f }; // 左下
	vertexDataTriangle_[3].texcoord = { 0.0f, 1.0f };
	vertexDataTriangle_[4].position = { 0.0f, 0.0f, 0.0f, 1.0f }; // 上
	vertexDataTriangle_[4].texcoord = { 0.5f, 0.0f };
	vertexDataTriangle_[5].position = { 0.5f, -0.5f, -0.5f, 1.0f }; // 右下
	vertexDataTriangle_[5].texcoord = { 1.0f, 1.0f };
	for (int i = 0; i < 6; ++i) {
		vertexDataTriangle_[i].normal = { 0.0f, 0.0f, -1.0f };
	}
#ifndef NDEBUG
	// ピッキング用のバウンディング球を頂点から算出（デバッグカメラ用）
	ComputeLocalBoundingSphere(vertexDataTriangle_, 6, localCenterTriangle_, localRadiusTriangle_);
#endif

	// --- OBJモデル読み込み ---
	modelData_ = LoadObjFile("resources", "axis.obj");
	vertexResourceObj_ = CreateBufferResource(device, sizeof(VertexData) * modelData_.vertices.size());
	vbvObj_.BufferLocation = vertexResourceObj_->GetGPUVirtualAddress();
	vbvObj_.SizeInBytes = UINT(sizeof(VertexData) * modelData_.vertices.size());
	vbvObj_.StrideInBytes = sizeof(VertexData);
	VertexData* objVertices = nullptr;
	vertexResourceObj_->Map(0, nullptr, reinterpret_cast<void**>(&objVertices));
	std::memcpy(objVertices, modelData_.vertices.data(), sizeof(VertexData) * modelData_.vertices.size());
#ifndef NDEBUG
	// ピッキング用のバウンディング球を頂点から算出（デバッグカメラ用）
	ComputeLocalBoundingSphere(modelData_.vertices.data(), modelData_.vertices.size(), localCenterObj_, localRadiusObj_);
#endif

	// --- 三角形・OBJ共通の3Dマテリアル ---
	material_ = CreateMaterialResource(device, true);

	// --- 球の頂点リソース ---
	sphereVertexCount_ = subdivision_ * subdivision_ * 6;
	vertexResourceSphere_ = CreateBufferResource(device, sizeof(VertexData) * sphereVertexCount_);
	GenerateSphere(subdivision_, vertexResourceSphere_.Get(), vbvSphere_, sphereVertexCount_);

	// --- スプライトの頂点リソース ---
	vertexResourceSprite_ = CreateBufferResource(device, sizeof(VertexData) * 4);
	vbvSprite_.BufferLocation = vertexResourceSprite_->GetGPUVirtualAddress();
	vbvSprite_.SizeInBytes = sizeof(VertexData) * 4;
	vbvSprite_.StrideInBytes = sizeof(VertexData);
	vertexResourceSprite_->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite_));
	vertexDataSprite_[0].position = { 0.0f, 360.0f, 0.0f, 1.0f }; // 左下
	vertexDataSprite_[1].position = { 0.0f, 0.0f, 0.0f, 1.0f }; // 左上
	vertexDataSprite_[2].position = { 640.0f, 360.0f, 0.0f, 1.0f }; // 右下
	vertexDataSprite_[3].position = { 640.0f, 0.0f, 0.0f, 1.0f }; // 右上
	vertexDataSprite_[0].texcoord = { 0.0f, 1.0f };
	vertexDataSprite_[1].texcoord = { 0.0f, 0.0f };
	vertexDataSprite_[2].texcoord = { 1.0f, 1.0f };
	vertexDataSprite_[3].texcoord = { 1.0f, 0.0f };
	for (int i = 0; i < 4; ++i) {
		vertexDataSprite_[i].normal = { 0.0f, 0.0f, -1.0f };
	}

	// --- スプライトのインデックスリソース ---
	indexResourceSprite_ = CreateBufferResource(device, sizeof(uint32_t) * 6);
	ibvSprite_.BufferLocation = indexResourceSprite_->GetGPUVirtualAddress();
	ibvSprite_.SizeInBytes = sizeof(uint32_t) * 6;
	ibvSprite_.Format = DXGI_FORMAT_R32_UINT;
	indexResourceSprite_->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite_));
	indexDataSprite_[0] = 0;
	indexDataSprite_[1] = 1;
	indexDataSprite_[2] = 2;
	indexDataSprite_[3] = 1;
	indexDataSprite_[4] = 3;
	indexDataSprite_[5] = 2;

	// --- スプライト用マテリアル（2Dなのでライティング無効）---
	spriteMaterial_ = CreateMaterialResource(device, false);

	// --- Transform用リソース ---
	triangleTransform_ = CreateTransformResource(device);
	sphereTransform_ = CreateTransformResource(device);
	objTransform_ = CreateTransformResource(device);
	spriteTransform_ = CreateTransformResource(device);

	// --- 平行光源 ---
	light_ = CreateDirectionalLight(device);

	// --- サウンド読み込み ---
	soundHandle_ = Audio::GetInstance()->LoadWave("resources/Alarm01.wav");
	Audio::GetInstance()->SetVolume(soundHandle_, soundVolume_);

	// --- 天球（背景。ライティング無効・カリング無効の専用PSO）---
	skydome_.Initialize(device, rootSignature, vertexShader, pixelShader);
}

void GameScene::Update() {
	// スペースキーを押した瞬間にサウンド再生（Enterはデバッグカメラの有効・無効切り替えに割り当て）
	if (Input::GetInstance()->IsTrigger(DIK_SPACE)) {
		Audio::GetInstance()->Play(soundHandle_);
	}

	// ゲームの処理
	transformTriangle_.rotate.y += 0.04f;
	transformSphere_.rotate.y += 0.02f;

	const float width = float(WinApp::kClientWidth);
	const float height = float(WinApp::kClientHeight);

	// 透視投影
	Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, width / height, 0.1f, 100.0f);

	// 通常カメラのビュー行列
	Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform_.scale, cameraTransform_.rotate, cameraTransform_.translate);
	Matrix4x4 viewMatrix = Inverse(cameraMatrix);

#ifndef NDEBUG
	// --- デバッグカメラ更新（Debugビルドのみ。Releaseでは丸ごと除外される）---
	// ピッキング対象（ワールド空間のバウンディング球）を毎フレーム組み立てる
	std::vector<DebugCamera::PickTarget> pickTargets;
	auto addPickTarget = [&pickTargets](const Transform3D& transform, const Vector3& localCenter, float localRadius) {
		Matrix4x4 world = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
		Vector3 center = Transform(localCenter, world);
		// 拡大率の最大成分で半径をスケールする（std::maxはWindows.hのマクロと衝突するため比較で求める）
		float maxScale = std::fabs(transform.scale.x);
		if (std::fabs(transform.scale.y) > maxScale) { maxScale = std::fabs(transform.scale.y); }
		if (std::fabs(transform.scale.z) > maxScale) { maxScale = std::fabs(transform.scale.z); }
		pickTargets.push_back({ center, localRadius * maxScale });
	};
	addPickTarget(transformTriangle_, localCenterTriangle_, localRadiusTriangle_);
	addPickTarget(transformSphere_, { 0.0f, 0.0f, 0.0f }, 1.0f);  // 球は半径1のユニット球
	addPickTarget(transformObj_, localCenterObj_, localRadiusObj_);

	// ImGuiがマウスを使用中はデバッグカメラのマウス操作を無視する
	bool blockMouse = false;
#ifdef USE_IMGUI
	blockMouse = ImGui::GetIO().WantCaptureMouse;
#endif
	debugCamera_.Update(pickTargets, width, height, projectionMatrix, blockMouse);

	// デバッグカメラ有効時は通常カメラのビューを上書きする
	if (debugCamera_.IsEnabled()) {
		viewMatrix = debugCamera_.GetViewMatrix();
	}
#endif  // !NDEBUG

	UpdateTransformMatrix(triangleTransform_, transformTriangle_, viewMatrix, projectionMatrix);
	UpdateTransformMatrix(sphereTransform_, transformSphere_, viewMatrix, projectionMatrix);
	UpdateTransformMatrix(objTransform_, transformObj_, viewMatrix, projectionMatrix);

	// 天球（カメラ追従ON時は中心がカメラ位置へ追従する）
	skydome_.Update(viewMatrix, projectionMatrix);

	// スプライト（正射影）
	Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite_.scale, transformSprite_.rotate, transformSprite_.translate);
	Matrix4x4 viewMatrixSprite = MakeIdentity4x4();
	Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, width, height, 0.0f, 100.0f);
	spriteTransform_.data->WVP = Multiply(worldMatrixSprite, Multiply(viewMatrixSprite, projectionMatrixSprite));
	spriteTransform_.data->World = worldMatrixSprite;

	// スプライトのUV変換行列
	Matrix4x4 uvTransformMatrix = MakeScaleMatrix(uvTransformSprite_.scale);
	uvTransformMatrix = Multiply(uvTransformMatrix, MakeRotateZMatrix(uvTransformSprite_.rotate.z));
	uvTransformMatrix = Multiply(uvTransformMatrix, MakeTranslateMatrix(uvTransformSprite_.translate));
	spriteMaterial_.data->uvTransform = uvTransformMatrix;
}

#ifdef USE_IMGUI
void GameScene::DrawImGui(ID3D12Device* device) {
	const char* textureItems[] = {
		"uvChecker",
		"monsterBall"
	};

	ImGui::Begin("3D Objects");

	// ----Triangle----
	if (ImGui::TreeNode("Triangle")) {
		ImGui::PushID("Triangle");

		ImGui::DragFloat3("scale", &transformTriangle_.scale.x, 0.01f);
		ImGui::DragFloat3("rotate", &transformTriangle_.rotate.x, 0.01f);
		ImGui::DragFloat3("translate", &transformTriangle_.translate.x, 0.01f);
		ImGui::Separator();
		ImGui::DragFloat4("Vertex0", &vertexDataTriangle_[0].position.x, 0.01f);
		ImGui::DragFloat4("Vertex1", &vertexDataTriangle_[1].position.x, 0.01f);
		ImGui::DragFloat4("Vertex2", &vertexDataTriangle_[2].position.x, 0.01f);
		ImGui::Separator();

		ImGui::ColorEdit4("Color", &material_.data->color.x);
		bool lighting = material_.data->enableLighting != 0;
		if (ImGui::Checkbox("Enable Lighting", &lighting)) {
			material_.data->enableLighting = lighting;
		}

		ImGui::Combo("Texture", reinterpret_cast<int*>(&triangleTextureIndex_), textureItems, IM_ARRAYSIZE(textureItems));

		ImGui::PopID();
		ImGui::TreePop();
	}

	// ----Sphere----
	if (ImGui::TreeNode("Sphere")) {
		ImGui::PushID("Sphere");

		ImGui::DragFloat3("scale", &transformSphere_.scale.x, 0.01f);
		ImGui::DragFloat3("rotate", &transformSphere_.rotate.x, 0.01f);
		ImGui::DragFloat3("translate", &transformSphere_.translate.x, 0.01f);
		ImGui::Separator();

		ImGui::DragInt("Sphere Subdivision", (int*)&subdivision_, 1, 3, 128);

		if (subdivision_ != prevSubdivision_) {
			// FenceでGPU完了待ちをしてから差し替える（ComPtrの代入で旧リソースは自動開放）
			DirectXCore::GetInstance()->WaitForGPU();

			sphereVertexCount_ = subdivision_ * subdivision_ * 6;
			vertexResourceSphere_ = CreateBufferResource(device, sizeof(VertexData) * sphereVertexCount_);
			GenerateSphere(subdivision_, vertexResourceSphere_.Get(), vbvSphere_, sphereVertexCount_);

			prevSubdivision_ = subdivision_;
		}
		ImGui::Separator();

		ImGui::Combo("Texture", reinterpret_cast<int*>(&sphereTextureIndex_), textureItems, IM_ARRAYSIZE(textureItems));

		ImGui::PopID();
		ImGui::TreePop();
	}

	// ----Obj----
	if (ImGui::TreeNode("Obj")) {
		ImGui::PushID("Obj");

		ImGui::DragFloat3("scale", &transformObj_.scale.x, 0.01f);
		ImGui::DragFloat3("rotate", &transformObj_.rotate.x, 0.01f);
		ImGui::DragFloat3("translate", &transformObj_.translate.x, 0.01f);
		ImGui::Separator();

		ImGui::ColorEdit4("Color", &material_.data->color.x);
		bool lighting = material_.data->enableLighting != 0;
		if (ImGui::Checkbox("Enable Lighting", &lighting)) {
			material_.data->enableLighting = lighting;
		}

		ImGui::Combo("Texture", reinterpret_cast<int*>(&objTextureIndex_), textureItems, IM_ARRAYSIZE(textureItems));

		ImGui::PopID();
		ImGui::TreePop();
	}

	// ----Skydome----
	skydome_.DrawImGui();

	ImGui::End();

	ImGui::Begin("2D Objects");
	if (ImGui::TreeNode("Square")) {
		ImGui::PushID("Square");

		ImGui::Checkbox("Draw Sprite", &drawSprite_);
		ImGui::Separator();

		ImGui::DragFloat3("scale", &transformSprite_.scale.x, 0.01f);
		ImGui::DragFloat3("rotate", &transformSprite_.rotate.x, 0.05f);
		ImGui::DragFloat3("translate", &transformSprite_.translate.x, 0.35f);
		ImGui::Separator();
		ImGui::DragFloat4("Vertex0 position", &vertexDataSprite_[0].position.x, 0.2f);
		ImGui::DragFloat4("Vertex1 position", &vertexDataSprite_[1].position.x, 0.2f);
		ImGui::DragFloat4("Vertex2 position", &vertexDataSprite_[2].position.x, 0.2f);
		ImGui::DragFloat4("Vertex3 position", &vertexDataSprite_[3].position.x, 0.2f);

		ImGui::DragFloat2("Vertex0 texcoord", &vertexDataSprite_[0].texcoord.x, 0.2f);
		ImGui::DragFloat2("Vertex1 texcoord", &vertexDataSprite_[1].texcoord.x, 0.2f);
		ImGui::DragFloat2("Vertex2 texcoord", &vertexDataSprite_[2].texcoord.x, 0.2f);
		ImGui::DragFloat2("Vertex3 texcoord", &vertexDataSprite_[3].texcoord.x, 0.2f);

		ImGui::Separator();

		ImGui::DragFloat2("UVTranslate", &uvTransformSprite_.translate.x, 0.01f, -10.0f, 10.0f);
		ImGui::DragFloat2("UVScale", &uvTransformSprite_.scale.x, 0.01f, -10.0f, 10.0f);
		ImGui::SliderAngle("UVRotate", &uvTransformSprite_.rotate.z);

		ImGui::Separator();

		bool lightingSprite = spriteMaterial_.data->enableLighting != 0;
		if (ImGui::Checkbox("Enable Lighting", &lightingSprite)) {
			spriteMaterial_.data->enableLighting = lightingSprite;
		}

		ImGui::Combo("Texture", reinterpret_cast<int*>(&spriteTextureIndex_), textureItems, IM_ARRAYSIZE(textureItems));

		ImGui::PopID();
		ImGui::TreePop();
	}
	ImGui::End();

	ImGui::Begin("Camera, DirectionalLight");

	ImGui::DragFloat3("Camera scale", &cameraTransform_.scale.x, 0.01f);
	ImGui::DragFloat3("Camera rotate", &cameraTransform_.rotate.x, 0.01f);
	ImGui::DragFloat3("Camera translate", &cameraTransform_.translate.x, 0.01f);

	ImGui::Separator();

	ImGui::ColorEdit4("Light Color", &light_.data->color.x);
	ImGui::DragFloat3("Light Direction", &light_.data->direction.x, 0.01f);
	ImGui::DragFloat("Intensity", &light_.data->intensity, 0.01f, 0.0f, 10.0f);

	ImGui::End();

#ifndef NDEBUG
	// デバッグカメラの状態表示・調整（Debugビルドのみ）
	debugCamera_.DrawImGui();
#endif

	ImGui::Begin("Sound");

	// ボタンでもSpaceキーでも再生できる（再生中は頭から鳴らし直す）
	if (ImGui::Button("Play (Alarm01)")) {
		Audio::GetInstance()->Play(soundHandle_);
	}

	// 音量を増減する
	if (ImGui::SliderFloat("Volume", &soundVolume_, 0.0f, 1.0f)) {
		Audio::GetInstance()->SetVolume(soundHandle_, soundVolume_);
	}

	ImGui::End();
}
#endif

void GameScene::Draw(
	ID3D12GraphicsCommandList* commandList,
	ID3D12RootSignature* rootSignature,
	ID3D12PipelineState* pipelineState,
	ID3D12DescriptorHeap* srvDescriptorHeap,
	const D3D12_GPU_DESCRIPTOR_HANDLE* textureHandles) {

	// --- 共通設定（Viewport/Scissor/RenderTargetはDirectXCore::BeginFrameで設定済み）---
	commandList->SetGraphicsRootSignature(rootSignature);
	ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap };
	commandList->SetDescriptorHeaps(1, descriptorHeaps);

	// --- 天球を最初に描画（背景。専用PSO=カリング無効に切り替わる）---
	skydome_.Draw(commandList, rootSignature, textureHandles[skydomeTextureIndex_], light_.resource.Get());

	// --- 以降は標準PSO（裏面カリング）で描画 ---
	commandList->SetPipelineState(pipelineState);
	// マテリアルと平行光源のCBufferを設定
	commandList->SetGraphicsRootConstantBufferView(0, material_.resource->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(2, light_.resource->GetGPUVirtualAddress());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// --- 三角形描画 ---
	DrawObject(
		commandList,
		vbvTriangle_,
		textureHandles[triangleTextureIndex_],
		triangleTransform_.resource.Get(),
		6);

	// --- 球描画 ---
	DrawObject(
		commandList,
		vbvSphere_,
		textureHandles[sphereTextureIndex_],
		sphereTransform_.resource.Get(),
		sphereVertexCount_);

	// --- OBJ描画 ---
	DrawObject(
		commandList,
		vbvObj_,
		textureHandles[objTextureIndex_],
		objTransform_.resource.Get(),
		UINT(modelData_.vertices.size()));

	// --- スプライト描画（インデックス6個でクアッド。drawSprite_がfalseなら描かない）---
	if (drawSprite_) {
		commandList->SetGraphicsRootDescriptorTable(3, textureHandles[spriteTextureIndex_]);
		commandList->IASetVertexBuffers(0, 1, &vbvSprite_);
		commandList->IASetIndexBuffer(&ibvSprite_);
		commandList->SetGraphicsRootConstantBufferView(0, spriteMaterial_.resource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootConstantBufferView(1, spriteTransform_.resource->GetGPUVirtualAddress());
		commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
	}
}

