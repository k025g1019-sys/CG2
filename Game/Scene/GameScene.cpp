#include "Game/Scene/GameScene.h"

#include <vector>

#include "Engine/Audio/Audio.h"
#include "Engine/Culling/FrustumCulling.h"
#include "Engine/Core/DirectXCore.h"
#include "Engine/Core/WinApp.h"
#include "Engine/Graphics/PipelineManager.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Input/Input.h"
#include "Engine/Math/Matrix4x4.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

void GameScene::Initialize() {
	ID3D12Device* device = DirectXCore::GetInstance()->GetDevice();

	// --- シーンで使うテクスチャ ---
	textureHandles_[0] = TextureManager::GetInstance()->Load("resources/uvChecker.png");
	textureHandles_[1] = TextureManager::GetInstance()->Load("resources/monsterBall.png");

	// --- 三角形（2枚。2枚目は1枚目を貫通する）---
	VertexData triangleVertices[6]{};
	triangleVertices[0].position = { -0.5f, -0.5f, 0.0f, 1.0f }; // 左下
	triangleVertices[0].texcoord = { 0.0f, 1.0f };
	triangleVertices[1].position = { 0.0f, 0.5f, 0.0f, 1.0f }; // 上
	triangleVertices[1].texcoord = { 0.5f, 0.0f };
	triangleVertices[2].position = { 0.5f, -0.5f, 0.0f, 1.0f }; // 右下
	triangleVertices[2].texcoord = { 1.0f, 1.0f };
	triangleVertices[3].position = { -0.5f, -0.5f, 0.5f, 1.0f }; // 左下
	triangleVertices[3].texcoord = { 0.0f, 1.0f };
	triangleVertices[4].position = { 0.0f, 0.0f, 0.0f, 1.0f }; // 上
	triangleVertices[4].texcoord = { 0.5f, 0.0f };
	triangleVertices[5].position = { 0.5f, -0.5f, -0.5f, 1.0f }; // 右下
	triangleVertices[5].texcoord = { 1.0f, 1.0f };
	for (VertexData& vertex : triangleVertices) {
		vertex.normal = { 0.0f, 0.0f, -1.0f };
	}
	triangleMesh_.Create(device, triangleVertices, 6);
	triangle_.Initialize(device, &triangleMesh_, textureHandles_[triangleTextureIndex_]);
	triangle_.GetTransform().translate = { 2.5f, 0.0f, 0.0f };

	// --- 球 ---
	sphereMesh_.CreateSphere(device, subdivision_);
	sphere_.Initialize(device, &sphereMesh_, textureHandles_[sphereTextureIndex_]);

	// --- OBJモデル ---
	objMesh_.CreateFromObj(device, "resources", "axis.obj");
	obj_.Initialize(device, &objMesh_, textureHandles_[objTextureIndex_]);
	obj_.GetTransform().rotate.y = 3.1415f;

	// --- スプライト ---
	sprite_.Initialize(device, textureHandles_[spriteTextureIndex_], { 640.0f, 360.0f });

	// --- 天球（背景。ライティング無効・カリング無効PSO）---
	skydome_.Initialize(device);

	// --- 平行光源 ---
	lightCB_.Create(device, DirectXCore::kFramesInFlight);

	// --- カメラ初期位置 ---
	camera_.GetTransform().rotate = { 0.04f, 0.0f, 0.0f };
	camera_.GetTransform().translate = { 0.0f, 1.7f, -10.0f };

	// --- サウンド読み込み ---
	soundHandle_ = Audio::GetInstance()->LoadWave("resources/Alarm01.wav");
	Audio::GetInstance()->SetVolume(soundHandle_, soundVolume_);
}

void GameScene::Update() {
	// スペースキーを押した瞬間にサウンド再生（Enterはデバッグカメラの有効・無効切り替えに割り当て）
	if (Input::GetInstance()->IsTrigger(DIK_SPACE)) {
		Audio::GetInstance()->Play(soundHandle_);
	}

	// ゲームの処理
	triangle_.GetTransform().rotate.y += 0.04f;
	sphere_.GetTransform().rotate.y += 0.02f;

	// 現在のウィンドウサイズを使う（リサイズに追従させ、アスペクト比の歪みを防ぐ）
	const float width = float(WinApp::GetInstance()->GetClientWidth());
	const float height = float(WinApp::GetInstance()->GetClientHeight());

	Matrix4x4 projection = camera_.GetProjectionMatrix(width / height);
	Matrix4x4 view = camera_.GetViewMatrix();

#ifndef NDEBUG
	// --- デバッグカメラ更新（Debugビルドのみ。Releaseでは丸ごと除外される）---
	// ピッキング対象（ワールド空間のバウンディング球）を毎フレーム組み立てる
	std::vector<DebugCamera::PickTarget> pickTargets;
	for (const Object3D* object : { &triangle_, &sphere_, &obj_ }) {
		Sphere sphere = object->CalcWorldBoundingSphere();
		pickTargets.push_back({ sphere.center, sphere.radius });
	}

	// ImGuiがマウスを使用中はデバッグカメラのマウス操作を無視する
	bool blockMouse = false;
#ifdef USE_IMGUI
	blockMouse = ImGui::GetIO().WantCaptureMouse;
#endif
	debugCamera_.Update(pickTargets, width, height, projection, blockMouse);

	// デバッグカメラ有効時は通常カメラのビューを上書きする
	if (debugCamera_.IsEnabled()) {
		view = debugCamera_.GetViewMatrix();
	}
#endif  // !NDEBUG

	// --- 各オブジェクトの更新（行列計算・定数バッファ書き込み・視錐台カリング）---
	Frustum3D frustum = MakeFrustumFromViewProjection(view * projection);
	triangle_.Update(view, projection, frustum);
	sphere_.Update(view, projection, frustum);
	obj_.Update(view, projection, frustum);

	// 天球（カメラ追従ON時は中心がカメラ位置へ追従する）
	skydome_.Update(view, projection);

	// スプライト（正射影・2Dカリング）
	sprite_.Update(width, height);

	// 平行光源
	lightCB_.Write(DirectXCore::GetInstance()->GetFrameIndex(), light_);
}

#ifdef USE_IMGUI
void GameScene::DrawImGui() {
	const char* textureItems[] = {
		"uvChecker",
		"monsterBall"
	};

	ImGui::Begin("3D Objects");

	// ----Triangle----
	if (ImGui::TreeNode("Triangle")) {
		ImGui::PushID("Triangle");

		Transform3D& transform = triangle_.GetTransform();
		ImGui::DragFloat3("scale", &transform.scale.x, 0.01f);
		ImGui::DragFloat3("rotate", &transform.rotate.x, 0.01f);
		ImGui::DragFloat3("translate", &transform.translate.x, 0.01f);
		ImGui::Separator();
		VertexData* vertices = triangleMesh_.GetMappedVertices();
		ImGui::DragFloat4("Vertex0", &vertices[0].position.x, 0.01f);
		ImGui::DragFloat4("Vertex1", &vertices[1].position.x, 0.01f);
		ImGui::DragFloat4("Vertex2", &vertices[2].position.x, 0.01f);
		ImGui::Separator();

		ImGui::ColorEdit4("Color", &triangle_.GetMaterial().color.x);
		bool lighting = triangle_.GetMaterial().enableLighting != 0;
		if (ImGui::Checkbox("Enable Lighting", &lighting)) {
			triangle_.GetMaterial().enableLighting = lighting;
		}

		if (ImGui::Combo("Texture", &triangleTextureIndex_, textureItems, IM_ARRAYSIZE(textureItems))) {
			triangle_.SetTextureHandle(textureHandles_[triangleTextureIndex_]);
		}

		ImGui::PopID();
		ImGui::TreePop();
	}

	// ----Sphere----
	if (ImGui::TreeNode("Sphere")) {
		ImGui::PushID("Sphere");

		Transform3D& transform = sphere_.GetTransform();
		ImGui::DragFloat3("scale", &transform.scale.x, 0.01f);
		ImGui::DragFloat3("rotate", &transform.rotate.x, 0.01f);
		ImGui::DragFloat3("translate", &transform.translate.x, 0.01f);
		ImGui::Separator();

		ImGui::DragInt("Sphere Subdivision", reinterpret_cast<int*>(&subdivision_), 1, 3, 128);

		if (subdivision_ != prevSubdivision_) {
			// FenceでGPU完了待ちをしてから差し替える（旧頂点バッファは自動開放）
			DirectXCore::GetInstance()->WaitForGPU();

			sphereMesh_.CreateSphere(DirectXCore::GetInstance()->GetDevice(), subdivision_);

			prevSubdivision_ = subdivision_;
		}
		ImGui::Separator();

		if (ImGui::Combo("Texture", &sphereTextureIndex_, textureItems, IM_ARRAYSIZE(textureItems))) {
			sphere_.SetTextureHandle(textureHandles_[sphereTextureIndex_]);
		}

		ImGui::PopID();
		ImGui::TreePop();
	}

	// ----Obj----
	if (ImGui::TreeNode("Obj")) {
		ImGui::PushID("Obj");

		Transform3D& transform = obj_.GetTransform();
		ImGui::DragFloat3("scale", &transform.scale.x, 0.01f);
		ImGui::DragFloat3("rotate", &transform.rotate.x, 0.01f);
		ImGui::DragFloat3("translate", &transform.translate.x, 0.01f);
		ImGui::Separator();

		ImGui::ColorEdit4("Color", &obj_.GetMaterial().color.x);
		bool lighting = obj_.GetMaterial().enableLighting != 0;
		if (ImGui::Checkbox("Enable Lighting", &lighting)) {
			obj_.GetMaterial().enableLighting = lighting;
		}

		if (ImGui::Combo("Texture", &objTextureIndex_, textureItems, IM_ARRAYSIZE(textureItems))) {
			obj_.SetTextureHandle(textureHandles_[objTextureIndex_]);
		}

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

		Transform3D& transform = sprite_.GetTransform();
		ImGui::DragFloat3("scale", &transform.scale.x, 0.01f);
		ImGui::DragFloat3("rotate", &transform.rotate.x, 0.05f);
		ImGui::DragFloat3("translate", &transform.translate.x, 0.35f);
		ImGui::Separator();
		VertexData* vertices = sprite_.GetMappedVertices();
		ImGui::DragFloat4("Vertex0 position", &vertices[0].position.x, 0.2f);
		ImGui::DragFloat4("Vertex1 position", &vertices[1].position.x, 0.2f);
		ImGui::DragFloat4("Vertex2 position", &vertices[2].position.x, 0.2f);
		ImGui::DragFloat4("Vertex3 position", &vertices[3].position.x, 0.2f);

		ImGui::DragFloat2("Vertex0 texcoord", &vertices[0].texcoord.x, 0.2f);
		ImGui::DragFloat2("Vertex1 texcoord", &vertices[1].texcoord.x, 0.2f);
		ImGui::DragFloat2("Vertex2 texcoord", &vertices[2].texcoord.x, 0.2f);
		ImGui::DragFloat2("Vertex3 texcoord", &vertices[3].texcoord.x, 0.2f);

		ImGui::Separator();

		Transform3D& uvTransform = sprite_.GetUVTransform();
		ImGui::DragFloat2("UVTranslate", &uvTransform.translate.x, 0.01f, -10.0f, 10.0f);
		ImGui::DragFloat2("UVScale", &uvTransform.scale.x, 0.01f, -10.0f, 10.0f);
		ImGui::SliderAngle("UVRotate", &uvTransform.rotate.z);

		ImGui::Separator();

		bool lightingSprite = sprite_.GetMaterial().enableLighting != 0;
		if (ImGui::Checkbox("Enable Lighting", &lightingSprite)) {
			sprite_.GetMaterial().enableLighting = lightingSprite;
		}

		if (ImGui::Combo("Texture", &spriteTextureIndex_, textureItems, IM_ARRAYSIZE(textureItems))) {
			sprite_.SetTextureHandle(textureHandles_[spriteTextureIndex_]);
		}

		ImGui::PopID();
		ImGui::TreePop();
	}
	ImGui::End();

	ImGui::Begin("Camera, DirectionalLight");

	Transform3D& cameraTransform = camera_.GetTransform();
	ImGui::DragFloat3("Camera scale", &cameraTransform.scale.x, 0.01f);
	ImGui::DragFloat3("Camera rotate", &cameraTransform.rotate.x, 0.01f);
	ImGui::DragFloat3("Camera translate", &cameraTransform.translate.x, 0.01f);

	ImGui::Separator();

	ImGui::ColorEdit4("Light Color", &light_.color.x);
	ImGui::DragFloat3("Light Direction", &light_.direction.x, 0.01f);
	ImGui::DragFloat("Intensity", &light_.intensity, 0.01f, 0.0f, 10.0f);

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

	// --- 視錐台カリングの判定結果表示 ---
	ImGui::Begin("Frustum Culling");
	auto visibilityText = [](FrustumVisibility visibility) -> const char* {
		switch (visibility) {
		case FrustumVisibility::Inside:    return "Inside";
		case FrustumVisibility::Intersect: return "Intersect";
		case FrustumVisibility::Outside:   return "Outside";
		}
		return "Unknown";
	};
	ImGui::Text("Triangle (sphere) : %s", visibilityText(triangle_.GetVisibility()));
	ImGui::Text("Sphere   (sphere) : %s", visibilityText(sphere_.GetVisibility()));
	ImGui::Text("Obj      (sphere) : %s", visibilityText(obj_.GetVisibility()));
	ImGui::Text("Sprite   (2D AABB): %s", visibilityText(sprite_.GetVisibility()));
	ImGui::End();
}
#endif

void GameScene::Draw(ID3D12GraphicsCommandList* commandList) {
	uint32_t frameIndex = DirectXCore::GetInstance()->GetFrameIndex();

	// --- 共通設定（Viewport/Scissor/RenderTarget/DescriptorHeapはDirectXCore::BeginFrameで設定済み）---
	commandList->SetGraphicsRootSignature(PipelineManager::GetInstance()->GetRootSignature());

	// --- 天球を最初に描画（背景。カリング無効PSOに切り替わる）---
	skydome_.Draw(commandList, lightCB_.GetGPUAddress(frameIndex));

	// --- 以降は標準PSO（裏面カリング）で描画 ---
	commandList->SetPipelineState(PipelineManager::GetInstance()->Get(PipelineManager::Pipeline::kStandard));
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	// 平行光源のCBufferはシーン共通（各オブジェクトのマテリアル・Transformは各自が設定する）
	commandList->SetGraphicsRootConstantBufferView(2, lightCB_.GetGPUAddress(frameIndex));

	triangle_.Draw(commandList);
	sphere_.Draw(commandList);
	obj_.Draw(commandList);

	// スプライト（drawSprite_がfalse、または画面外なら描かれない）
	if (drawSprite_) {
		sprite_.Draw(commandList);
	}
}
