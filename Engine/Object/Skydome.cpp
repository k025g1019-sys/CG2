#include "Engine/Object/Skydome.h"

#include <cstring>

#include "GpuResource.h"
#include "PipelineManager.h"
#include "Matrix4x4.h"
#include "VertexData.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

void Skydome::Initialize(
	ID3D12Device* device,
	ID3D12RootSignature* rootSignature,
	IDxcBlob* vertexShader,
	IDxcBlob* pixelShader) {

	// --- モデル読み込み（半径1のユニット球）---
	modelData_ = LoadObjFile("resources", "skydome.obj");
	vertexCount_ = uint32_t(modelData_.vertices.size());

	vertexResource_ = CreateBufferResource(device, sizeof(VertexData) * vertexCount_);
	vbv_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vbv_.SizeInBytes = UINT(sizeof(VertexData) * vertexCount_);
	vbv_.StrideInBytes = sizeof(VertexData);
	VertexData* vertices = nullptr;
	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertices));
	std::memcpy(vertices, modelData_.vertices.data(), sizeof(VertexData) * vertexCount_);

	// --- マテリアル（ライティング無効でテクスチャをそのまま表示）---
	material_ = CreateMaterialResource(device, false);

	// --- Transform ---
	transform_ = CreateTransformResource(device);

	// --- 専用PSO（内側から見えるようカリングを無効化。シェーダー・RSは標準を流用）---
	pipelineState_ = PipelineManager::CreateStandardPipeline(
		device, rootSignature, vertexShader, pixelShader, D3D12_CULL_MODE_NONE);
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

	Transform3D world{
		{ scale_, scale_, scale_ },
		{ 0.0f, 0.0f, 0.0f },
		center
	};
	UpdateTransformMatrix(transform_, world, view, projection);
}

void Skydome::Draw(
	ID3D12GraphicsCommandList* commandList,
	ID3D12RootSignature* rootSignature,
	D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
	ID3D12Resource* lightResource) {

	// 専用PSO（カリング無効）に切り替える。DescriptorHeapは呼び出し側で設定済みの前提。
	commandList->SetGraphicsRootSignature(rootSignature);
	commandList->SetPipelineState(pipelineState_.Get());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 共通ルートシグネチャの各スロットを設定（0:Material[PS] / 1:Transform[VS] / 2:Light[PS] / 3:Texture[PS]）
	commandList->SetGraphicsRootConstantBufferView(0, material_.resource->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(2, lightResource->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(1, transform_.resource->GetGPUVirtualAddress());
	commandList->SetGraphicsRootDescriptorTable(3, textureHandle);

	commandList->IASetVertexBuffers(0, 1, &vbv_);
	commandList->DrawInstanced(vertexCount_, 1, 0, 0);
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
