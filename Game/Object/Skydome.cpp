#include "Game/Object/Skydome.h"

#include "Engine/Core/DirectXCore.h"
#include "Engine/Graphics/PipelineManager.h"
#include "Engine/Graphics/TextureManager.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

void Skydome::Initialize(ID3D12Device* device) {

	// --- モデル読み込み（半径1のユニット球）---
	mesh_.CreateFromObj(device, "resources", "skydome.obj");

	// --- テクスチャ ---
	textureHandle_ = TextureManager::GetInstance()->Load("resources/sky_sphere.png");

	// --- マテリアル（ライティング無効でテクスチャをそのまま表示）---
	material_.color = { 1.0f, 1.0f, 1.0f, 1.0f };
	material_.enableLighting = false;
	material_.uvTransform = MakeIdentity4x4();
	materialCB_.Create(device, DirectXCore::kFramesInFlight);

	// --- Transform ---
	transformCB_.Create(device, DirectXCore::kFramesInFlight);
}

void Skydome::Update(const Matrix4x4& view, const Matrix4x4& projection) {
	// カメラ追従ON時は中心をカメラのワールド位置へ合わせ、どこへ動いても境界が見えないようにする。
	// ビュー行列の逆行列がカメラのワールド行列なので、その原点を変換して位置を得る。
	Vector3 center = position_;
	if (followCamera_) {
		Matrix4x4 cameraWorld = Inverse(view);
		Vector3 origin{ 0.0f, 0.0f, 0.0f };
		center = Transform(origin, cameraWorld);
	}

	Matrix4x4 world = MakeAffineMatrix({ scale_, scale_, scale_ }, { 0.0f, 0.0f, 0.0f }, center);
	TransformationMatrix transformData{ world * view * projection, world };

	uint32_t frameIndex = DirectXCore::GetInstance()->GetFrameIndex();
	transformCB_.Write(frameIndex, transformData);
	materialCB_.Write(frameIndex, material_);
}

void Skydome::Draw(
	ID3D12GraphicsCommandList* commandList,
	D3D12_GPU_VIRTUAL_ADDRESS lightAddress) {

	// カリング無効PSOに切り替える。RootSignature・DescriptorHeapは呼び出し側で設定済みの前提。
	commandList->SetPipelineState(PipelineManager::GetInstance()->Get(PipelineManager::Pipeline::kNoCull));
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 共通ルートシグネチャの各スロットを設定（0:Material[PS] / 1:Transform[VS] / 2:Light[PS] / 3:Texture[PS]）
	uint32_t frameIndex = DirectXCore::GetInstance()->GetFrameIndex();
	commandList->SetGraphicsRootConstantBufferView(0, materialCB_.GetGPUAddress(frameIndex));
	commandList->SetGraphicsRootConstantBufferView(1, transformCB_.GetGPUAddress(frameIndex));
	commandList->SetGraphicsRootConstantBufferView(2, lightAddress);
	commandList->SetGraphicsRootDescriptorTable(3, TextureManager::GetInstance()->GetSrvHandleGPU(textureHandle_));

	mesh_.Draw(commandList);
}

#ifdef USE_IMGUI
void Skydome::DrawImGui() {
	if (ImGui::TreeNode("Skydome")) {
		ImGui::PushID("Skydome");

		ImGui::Checkbox("Follow Camera", &followCamera_);
		ImGui::DragFloat("Scale", &scale_, 0.1f, 1.0f, 90.0f);
		// 原点固定時のみ中心位置を調整できる（追従中はカメラ位置で上書きされる）
		if (!followCamera_) {
			ImGui::DragFloat3("Position", &position_.x, 0.1f);
		}

		ImGui::PopID();
		ImGui::TreePop();
	}
}
#endif
